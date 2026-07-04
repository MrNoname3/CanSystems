"""Unit tests for otaUpdate.py: config, payload derivation, and OTA framing.

Covers the pure / wire-level parts (port + topic + JSON + firmware-id derivation, and
the piece-splitting that builds the OTA frames) with no MQTT network I/O — the transfer's
MQTT client is swapped for a recorder. The whole module skips if the otaUpdate runtime
deps (paho-mqtt, tqdm, pyyaml) are absent, so it never hard-fails a deps-less environment.
"""

import base64
import hashlib
import json
import sys
import time
from pathlib import Path
from typing import Any, cast

import pytest

# otaUpdate imports paho/tqdm/yaml at module load; skip everything if they are missing.
pytest.importorskip("paho.mqtt.client")
pytest.importorskip("tqdm")
pytest.importorskip("yaml")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # ota/ for `import otaUpdate`
import otaUpdate as ota  # noqa: E402


def _write(tmp_path: Path, name: str, data: bytes) -> Path:
    path = tmp_path / name
    path.write_bytes(data)
    return path


@pytest.fixture(autouse=True)
def _stub_tqdm(monkeypatch: pytest.MonkeyPatch) -> None:  # pyright: ignore[reportUnusedFunction]  # pytest autouse fixture
    """Replace tqdm with a no-op so the state-machine tests draw no progress bars."""
    class _StubBar:
        def __init__(self, *args: Any, **kwargs: Any) -> None:
            pass

        def update(self, _n: object) -> None:
            pass

        def close(self) -> None:
            pass

    monkeypatch.setattr(ota, "tqdm", _StubBar)


# --- MQTTConfig: default port resolution and validation --------------------

@pytest.mark.parametrize("protocol, tls, expected_port", [
    ("mqtt", False, 1883),
    ("mqtt", True, 8883),
    ("ws", False, 80),
    ("ws", True, 443),
])
def test_mqttconfig_default_port(protocol: str, tls: bool, expected_port: int) -> None:
    assert ota.MQTTConfig(protocol=protocol, host="broker", tls_enabled=tls).port == expected_port


def test_mqttconfig_explicit_port_kept() -> None:
    assert ota.MQTTConfig(host="broker", port=12345).port == 12345


def test_mqttconfig_autogenerates_client_id() -> None:
    assert ota.MQTTConfig(host="broker").client_id.startswith("Python_OTA_")


def test_mqttconfig_bad_protocol() -> None:
    with pytest.raises(ValueError):
        ota.MQTTConfig(protocol="amqp", host="broker")


def test_mqttconfig_port_out_of_range() -> None:
    with pytest.raises(ValueError):
        ota.MQTTConfig(host="broker", port=70000)


def test_mqttconfig_empty_host() -> None:
    with pytest.raises(ValueError):
        ota.MQTTConfig(host="   ")


# --- ConfigManager: secrets.yaml loading ------------------------------------

def _write_secrets(tmp_path: Path, content: str) -> "ota.ConfigManager":
    """Write a secrets.yaml into tmp_path and return a ConfigManager rooted there."""
    (tmp_path / "secrets.yaml").write_text(content, encoding="utf-8")
    return ota.ConfigManager(str(tmp_path / "otaUpdate.py"))


def test_broker_section_builds_mqtt_config(tmp_path: Path) -> None:
    manager = _write_secrets(tmp_path, """
broker:
  protocol: ws
  host: broker.example.com
  username: user
  password: pass
  tls_enabled: true
""")
    config = manager.load_mqtt_config()
    assert config.protocol == "ws"
    assert config.host == "broker.example.com"
    assert config.port == 443  # ws + TLS default
    assert config.username == "user"
    assert config.tls_enabled is True


def test_relative_cafile_resolves_against_ota_dir(tmp_path: Path) -> None:
    (tmp_path / "ca.crt").write_bytes(b"cert")
    manager = _write_secrets(tmp_path, """
broker:
  host: broker
  tls_enabled: true
  cafile: ca.crt
""")
    assert manager.load_mqtt_config().cafile == str(tmp_path / "ca.crt")


def test_missing_secrets_file_raises(tmp_path: Path) -> None:
    manager = ota.ConfigManager(str(tmp_path / "otaUpdate.py"))
    with pytest.raises(FileNotFoundError):
        manager.load_mqtt_config()


def test_missing_broker_section_raises(tmp_path: Path) -> None:
    manager = _write_secrets(tmp_path, "devices: {}\n")
    with pytest.raises(ValueError):
        manager.load_mqtt_config()


def test_device_secrets_merge_defaults_and_overrides(tmp_path: Path) -> None:
    manager = _write_secrets(tmp_path, """
server_defaults:
  mqttServerUrl: broker.example.com
  mqttServerPort: 8883
devices:
  aabbccddeeff:
    mqttUserName: dev
    mqttPassword: secret
    mqttServerPort: 443
""")
    secrets = manager.device_server_secrets("aabbccddeeff")
    assert secrets == {
        "mqttServerUrl": "broker.example.com",
        "mqttServerPort": 443,  # device entry overrides the default
        "mqttUserName": "dev",
        "mqttPassword": "secret",
    }


def test_device_secrets_unknown_mac_raises(tmp_path: Path) -> None:
    manager = _write_secrets(tmp_path, "devices: {aabbccddeeff: {mqttUserName: x}}\n")
    with pytest.raises(ValueError):
        manager.device_server_secrets("000000000000")


def test_pio_command_override(tmp_path: Path) -> None:
    manager = _write_secrets(tmp_path, "pio: /custom/pio\n")
    assert manager.pio_command() == "/custom/pio"


# --- DeviceConfig: topic construction --------------------------------------

def test_deviceconfig_topics() -> None:
    device = ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x")
    assert device.send_topic == "iot/stod/AABBCCDDEEFF/common"
    assert device.receive_topic == "iot/dtos/AABBCCDDEEFF/common"


# --- FirmwareManager: size, md5, firmware id -------------------------------

def test_firmware_size_and_md5(tmp_path: Path) -> None:
    blob = b"\x01\x02project_esp32_can\x00trailing"
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", blob))
    assert firmware.size == len(blob)
    assert firmware.md5 == hashlib.md5(blob).hexdigest()


def test_firmware_id_extracted(tmp_path: Path) -> None:
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", b"....project_esp8266_thermo\x00...."))
    assert firmware.firmware_id == "project_esp8266_thermo"


def test_firmware_id_missing_raises(tmp_path: Path) -> None:
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", b"no id here"))
    with pytest.raises(ValueError):
        _ = firmware.firmware_id


# --- FileDataProvider: JSON minification + comment stripping ---------------

def test_json_is_serialized_compact(tmp_path: Path) -> None:
    src = '{\n  "a": 1,\n  "b": [1, 2, 3]\n}\n'
    provider = ota.FileDataProvider(_write(tmp_path, "config.json", src.encode()))
    assert provider.data == b'{"a":1,"b":[1,2,3]}'


def test_json_comments_stripped(tmp_path: Path) -> None:
    src = '{\n  // line comment\n  "a": 1, /* block */ "b": "//not a comment"\n}'
    provider = ota.FileDataProvider(_write(tmp_path, "config.json", src.encode()))
    assert provider.data == b'{"a":1,"b":"//not a comment"}'


def test_non_json_passthrough(tmp_path: Path) -> None:
    blob = b"\x00\x01raw bytes\xff"
    provider = ota.FileDataProvider(_write(tmp_path, "blob.bin", blob))
    assert provider.data == blob


def test_invalid_json_raises(tmp_path: Path) -> None:
    provider = ota.FileDataProvider(_write(tmp_path, "bad.json", b"{not valid"))
    with pytest.raises(ValueError):
        _ = provider.data


# --- server.json renderer ----------------------------------------------------

_SECRET_FIELDS = {
    "mqttUserName": "dev",
    "mqttPassword": "secret",
    "mqttServerUrl": "broker.example.com",
    "mqttServerPort": 8883,
}


def test_render_server_json_compact_and_ordered() -> None:
    rendered = ota.render_server_json(
        {**_SECRET_FIELDS, "ssid": "wifi", "password": "wpa"},
        {"haDiscovery": True},
    )
    assert rendered == (
        b'{"mqttUserName":"dev","mqttPassword":"secret","mqttServerUrl":"broker.example.com",'
        b'"mqttServerPort":8883,"haDiscovery":true,"ssid":"wifi","password":"wpa"}'
    )


def test_render_server_json_optional_fields_omitted() -> None:
    parsed = json.loads(ota.render_server_json(_SECRET_FIELDS, {}))
    assert set(parsed) == set(_SECRET_FIELDS)  # no haDiscovery / ssid / password key emitted


def test_render_server_json_missing_required_raises() -> None:
    incomplete = {k: v for k, v in _SECRET_FIELDS.items() if k != "mqttPassword"}
    with pytest.raises(ValueError, match="mqttPassword"):
        ota.render_server_json(incomplete, {})


def test_render_server_json_unknown_field_raises() -> None:
    with pytest.raises(ValueError, match="mqttUsername"):
        ota.render_server_json({**_SECRET_FIELDS, "mqttUsername": "typo"}, {})


# --- DeviceManager: file entry parsing (local_path vs render) ---------------

def _file_parser(tmp_path: Path) -> "ota.DeviceManager":
    return ota.DeviceManager(str(tmp_path / "otaUpdate.py"))


def test_parse_file_render_entry(tmp_path: Path) -> None:
    entry = _file_parser(tmp_path)._parse_file(
        {"name": "Server config", "render": "server_json", "device_path": "/config/server.json"},
        mac="aabbccddeeff",
    )
    assert entry.render == "server_json"
    assert entry.local_path is None


def test_parse_file_local_and_render_conflict(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        _file_parser(tmp_path)._parse_file(
            {"name": "x", "local_path": "a", "render": "server_json", "device_path": "/x"},
            mac="aabbccddeeff",
        )


def test_parse_file_neither_source_raises(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        _file_parser(tmp_path)._parse_file({"name": "x", "device_path": "/x"}, mac="aabbccddeeff")


def test_parse_file_unknown_render_type(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        _file_parser(tmp_path)._parse_file(
            {"name": "x", "render": "nope", "device_path": "/x"}, mac="aabbccddeeff"
        )


def test_parse_file_inline_content_entry(tmp_path: Path) -> None:
    entry = _file_parser(tmp_path)._parse_file(
        {"name": "Tube config", "content": {"tube": 1}, "device_path": "/config/tube.json"},
        mac="aabbccddeeff",
    )
    assert entry.content == {"tube": 1}
    assert entry.local_path is None and entry.render is None


def test_parse_file_content_must_be_mapping(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        _file_parser(tmp_path)._parse_file(
            {"name": "x", "content": [1, 2], "device_path": "/x"}, mac="aabbccddeeff"
        )


def test_parse_file_content_and_render_conflict(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        _file_parser(tmp_path)._parse_file(
            {"name": "x", "content": {}, "render": "server_json", "device_path": "/x"},
            mac="aabbccddeeff",
        )


# --- build_file_provider ------------------------------------------------------

def _device_with_secrets(tmp_path: Path) -> "tuple[ota.DeviceEntry, ota.ConfigManager]":
    manager = _write_secrets(tmp_path, """
server_defaults:
  mqttServerUrl: broker.example.com
  mqttServerPort: 8883
devices:
  aabbccddeeff:
    mqttUserName: dev
    mqttPassword: secret
""")
    device = ota.DeviceEntry(mac="aabbccddeeff", server_config={"haDiscovery": True})
    return device, manager


def test_provider_renders_server_json(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="Server config", device_path="/config/server.json", render="server_json")
    provider = ota.build_file_provider(entry, device, manager)
    parsed = json.loads(provider.data)
    assert parsed["mqttUserName"] == "dev"
    assert parsed["haDiscovery"] is True
    assert provider.size == len(provider.data)
    assert provider.md5 == hashlib.md5(provider.data).hexdigest()


def test_provider_reads_local_file(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    source = _write(tmp_path, "blob.bin", b"\x00\x01")
    entry = ota.FileEntry(name="blob", device_path="/blob.bin", local_path=source)
    assert ota.build_file_provider(entry, device, manager).data == b"\x00\x01"


def test_provider_serializes_inline_content(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="Tube config", device_path="/config/tube.json",
                          content={"tube": 1})
    assert ota.build_file_provider(entry, device, manager).data == b'{"tube":1}'


def test_provider_missing_local_file_raises(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="gone", device_path="/gone", local_path=tmp_path / "gone")
    with pytest.raises(FileNotFoundError):
        ota.build_file_provider(entry, device, manager)


def test_rendered_transfer_start_message(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="Server config", device_path="/config/server.json", render="server_json")
    provider = ota.build_file_provider(entry, device, manager)
    transfer = ota.FileTransfer(
        ota.DeviceConfig(mac_address="aabbccddeeff", project_name="x"),
        ota.MQTTConfig(host="broker"),
        entry,
        provider,
    )
    message = transfer._build_start_message()
    assert message["name"] == "/config/server.json"
    assert message["fileSize"] == provider.size
    assert message["md5"] == provider.md5
    assert "binId" not in message


# --- Provisioner: data/ materialization + uploadfs invocation ---------------

class _FakePioRun:
    """Stands in for subprocess.run: records the command and snapshots data/."""

    def __init__(self, repo_root: Path, returncode: int = 0) -> None:
        self.repo_root = repo_root
        self.returncode = returncode
        self.commands: list[list[str]] = []
        self.data_snapshot: dict[str, bytes] = {}

    def __call__(self, command: list[str], **kwargs: Any) -> Any:
        self.commands.append(command)
        data_dir = self.repo_root / "data"
        self.data_snapshot = {
            str(p.relative_to(data_dir)): p.read_bytes()
            for p in sorted(data_dir.rglob("*")) if p.is_file()
        }

        class _Result:
            returncode = self.returncode

        return _Result()


def _provision_setup(tmp_path: Path) -> "tuple[ota.ProjectEntry, ota.DeviceEntry, ota.ConfigManager, Path]":
    """A repo root with an ota/ dir, secrets.yaml, a cert file, and one device."""
    repo_root = tmp_path / "repo"
    ota_dir = repo_root / "ota"
    ota_dir.mkdir(parents=True)
    (ota_dir / "secrets.yaml").write_text("""
server_defaults:
  mqttServerUrl: broker.example.com
  mqttServerPort: 8883
devices:
  aabbccddeeff:
    mqttUserName: dev
    mqttPassword: secret
""", encoding="utf-8")
    (ota_dir / "ca.crt").write_bytes(b"CERTBYTES")

    device = ota.DeviceEntry(
        mac="aabbccddeeff",
        server_config={"haDiscovery": True},
        files=[
            ota.FileEntry(name="CA cert", device_path="/config/mosq-ca.crt",
                          local_path=ota_dir / "ca.crt"),
            ota.FileEntry(name="Server config", device_path="/config/server.json",
                          render="server_json"),
            ota.FileEntry(name="CAN firmware", device_path="/canAlertFw.bin",
                          local_path=ota_dir / "missing.bin"),  # not /config/* -> must be skipped
        ],
    )
    project = ota.ProjectEntry(name="Thermo", pio_project="project_esp8266_thermo", devices=[device])
    manager = ota.ConfigManager(str(ota_dir / "otaUpdate.py"))
    return project, device, manager, repo_root


def test_provision_materializes_config_files_and_runs_uploadfs(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    project, device, manager, repo_root = _provision_setup(tmp_path)
    fake_run = _FakePioRun(repo_root)
    monkeypatch.setattr(ota.subprocess, "run", fake_run)

    provisioner = ota.Provisioner(repo_root, "pio")
    assert provisioner.provision(project, device, manager) is True

    # pio was invoked with the project's environment and the uploadfs target.
    assert fake_run.commands == [["pio", "run", "-e", "project_esp8266_thermo", "-t", "uploadfs"]]
    # At upload time data/ held exactly the /config/* files (the CAN fw entry is skipped).
    assert set(fake_run.data_snapshot) == {"config/mosq-ca.crt", "config/server.json"}
    assert fake_run.data_snapshot["config/mosq-ca.crt"] == b"CERTBYTES"
    assert json.loads(fake_run.data_snapshot["config/server.json"])["mqttUserName"] == "dev"
    # data/ is cleared again afterwards.
    assert list((repo_root / "data").iterdir()) == []


def test_provision_clears_stale_data_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    project, device, manager, repo_root = _provision_setup(tmp_path)
    stale = repo_root / "data" / "leftover.bin"
    stale.parent.mkdir(parents=True)
    stale.write_bytes(b"junk")
    fake_run = _FakePioRun(repo_root)
    monkeypatch.setattr(ota.subprocess, "run", fake_run)

    assert ota.Provisioner(repo_root, "pio").provision(project, device, manager) is True
    assert "leftover.bin" not in fake_run.data_snapshot


def test_provision_failed_upload_returns_false_and_cleans_up(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    project, device, manager, repo_root = _provision_setup(tmp_path)
    monkeypatch.setattr(ota.subprocess, "run", _FakePioRun(repo_root, returncode=1))

    assert ota.Provisioner(repo_root, "pio").provision(project, device, manager) is False
    assert list((repo_root / "data").iterdir()) == []


def test_provision_without_config_entries_raises(tmp_path: Path) -> None:
    project, _device, manager, repo_root = _provision_setup(tmp_path)
    bare = ota.DeviceEntry(mac="aabbccddeeff", files=[])
    with pytest.raises(ValueError):
        ota.Provisioner(repo_root, "pio").provision(project, bare, manager)


def test_provision_missing_pio_returns_false(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    project, device, manager, repo_root = _provision_setup(tmp_path)

    def raise_missing(command: list[str], **kwargs: Any) -> Any:
        raise FileNotFoundError(command[0])

    monkeypatch.setattr(ota.subprocess, "run", raise_missing)
    assert ota.Provisioner(repo_root, "/nonexistent/pio").provision(project, device, manager) is False
    assert list((repo_root / "data").iterdir()) == []


# --- OTA framing: the piece-splitting that builds the wire frames -----------

class _RecordingMQTT:
    """Stands in for MQTTClient: records publish(topic, payload) instead of networking."""

    def __init__(self) -> None:
        self.published: list[tuple[str, str]] = []

    def publish(self, topic: str, payload: str) -> None:
        self.published.append((topic, payload))


def _make_updater(tmp_path: Path, firmware: bytes) -> "ota.OTAUpdater":
    device = ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x")
    updater = ota.OTAUpdater(device, ota.MQTTConfig(host="broker"), _write(tmp_path, "firmware.bin", firmware))
    updater.mqtt_client = _RecordingMQTT()  # type: ignore  # deliberate test double for the MQTT client
    return updater


def _drain_pieces(updater: "ota.OTAUpdater") -> list[dict[str, Any]]:
    """Drive _send_piece until every byte is sent; return the decoded piece payloads."""
    updater.remaining_bytes = updater.size
    recorder = cast(_RecordingMQTT, updater.mqtt_client)
    pieces: list[dict[str, Any]] = []
    while updater.remaining_bytes > 0:
        index = len(recorder.published)
        updater._send_piece()
        topic, payload = recorder.published[index]
        assert topic == updater.device_config.send_topic
        pieces.append(json.loads(payload))
    return pieces


def test_piece_splitting_reconstructs_firmware(tmp_path: Path) -> None:
    firmware = bytes(range(256)) * 3  # 768 bytes -> 7 full pieces of 100 + a 68-byte remainder
    updater = _make_updater(tmp_path, firmware)
    pieces = _drain_pieces(updater)

    assert [p["piece"] for p in pieces] == list(range(len(pieces)))  # contiguous 0..n
    assert len(pieces) == 8
    decoded = [base64.b64decode(p["data"]) for p in pieces]
    assert all(len(d) == updater.piece_size for d in decoded[:-1])
    assert len(decoded[-1]) == len(firmware) % updater.piece_size
    assert b"".join(decoded) == firmware
    assert updater.remaining_bytes == 0


def test_exact_multiple_of_piece_size(tmp_path: Path) -> None:
    updater = _make_updater(tmp_path, b"A" * 200)  # exactly two full pieces, no remainder
    pieces = _drain_pieces(updater)
    assert len(pieces) == 2
    assert all(len(base64.b64decode(p["data"])) == 100 for p in pieces)


def test_single_short_piece(tmp_path: Path) -> None:
    updater = _make_updater(tmp_path, b"hello")  # smaller than piece_size -> one piece
    pieces = _drain_pieces(updater)
    assert len(pieces) == 1
    assert base64.b64decode(pieces[0]["data"]) == b"hello"


# --- OTA start message -----------------------------------------------------

def test_start_message_fields(tmp_path: Path) -> None:
    firmware = b"....project_esp32_can\x00...."
    message = _make_updater(tmp_path, firmware)._build_start_message()
    assert message["name"] == "espFirmware"
    assert message["fileSize"] == len(firmware)
    assert message["binId"] == "project_esp32_can"
    assert len(message["md5"]) == 32


def test_file_start_message_fields(tmp_path: Path) -> None:
    source = _write(tmp_path, "config.json", b'{"a": 1}')
    entry = ota.FileEntry(name="config", local_path=source, device_path="/config.json")
    transfer = ota.FileTransfer(
        ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x"),
        ota.MQTTConfig(host="broker"),
        entry,
    )
    message = transfer._build_start_message()
    assert message["name"] == "/config.json"
    assert message["fileSize"] == len(transfer.data)
    assert len(message["md5"]) == 32
    assert "binId" not in message  # a file transfer omits binId; that is how the device tells it from firmware


# --- OTA command message ---------------------------------------------------

def test_command_message_fields() -> None:
    sender = ota.CommandSender(
        ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x"),
        ota.MQTTConfig(host="broker"),
        ota.CommandEntry(name="reboot", cmd="reboot"),
    )
    sender.mqtt_client = _RecordingMQTT()  # type: ignore  # deliberate test double for the MQTT client
    sender._send_command()
    recorder = cast(_RecordingMQTT, sender.mqtt_client)
    topic, payload = recorder.published[0]
    assert topic == sender.device_config.send_topic
    assert json.loads(payload) == {"cmd": "reboot"}


# --- OTA protocol state machine (sender side) ------------------------------

def _updater_in_state(tmp_path: Path, state: "ota.TransferState", *, remaining: int = 0,
                      firmware: bytes = b"x" * 250) -> "ota.OTAUpdater":
    updater = _make_updater(tmp_path, firmware)
    updater.state = state
    updater.remaining_bytes = remaining
    return updater


def test_start_ack_begins_sending(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_START_ACK, remaining=250)
    updater._process_response({"type": 1})
    assert updater.state == ota.TransferState.SENDING_FW


def test_start_nack_errors(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_START_ACK, remaining=250)
    updater._process_response({"type": 0})
    assert updater.state == ota.TransferState.ERROR


def test_piece_ack_with_remaining_continues(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_PIECE_ACK, remaining=50)
    updater._process_response({"type": 1})
    assert updater.state == ota.TransferState.SENDING_FW


def test_piece_ack_when_done_waits_for_check(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_PIECE_ACK, remaining=0)
    updater._process_response({"type": 1})
    assert updater.state == ota.TransferState.WAIT_CHECK_ACK


def test_check_ack_completes(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_CHECK_ACK)
    updater._process_response({"type": 1})
    assert updater.state == ota.TransferState.DONE


def test_response_without_type_is_ignored(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_PIECE_ACK, remaining=50)
    updater._process_response({"err": 7})
    assert updater.state == ota.TransferState.WAIT_PIECE_ACK  # unchanged


def test_timeout_in_wait_state_errors(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_PIECE_ACK, remaining=50)
    updater.timer_start = time.time() - (updater.timeout_seconds + 1)
    updater._process_state()
    assert updater.state == ota.TransferState.ERROR


def test_no_timeout_within_window(tmp_path: Path) -> None:
    updater = _updater_in_state(tmp_path, ota.TransferState.WAIT_PIECE_ACK, remaining=50)
    updater.timer_start = time.time()
    updater._process_state()
    assert updater.state == ota.TransferState.WAIT_PIECE_ACK


def _run_simulated_transfer(transfer: "ota._BaseTransfer", *, ack: bool = True, max_ticks: int = 10000) -> None:
    """Drive a sender (OTAUpdater or FileTransfer) to completion with a
    simulated device that ACKs/NACKs every wait."""
    waits = {
        ota.TransferState.WAIT_START_ACK,
        ota.TransferState.WAIT_PIECE_ACK,
        ota.TransferState.WAIT_CHECK_ACK,
    }
    transfer._send_start_message()
    ticks = 0
    while transfer.state not in (ota.TransferState.DONE, ota.TransferState.ERROR):
        ticks += 1
        assert ticks < max_ticks, "state machine did not terminate"
        if transfer.state in waits:
            transfer._pending_messages.append({"type": 1 if ack else 0})
        transfer._process_state()


def test_full_transfer_reaches_done(tmp_path: Path) -> None:
    firmware = b"project_e2e\x00" + bytes(range(256)) * 3  # valid firmware id + multi-piece body
    updater = _make_updater(tmp_path, firmware)
    _run_simulated_transfer(updater)

    assert updater.state == ota.TransferState.DONE
    # published[0] is the start message; the rest are the data pieces
    recorder = cast(_RecordingMQTT, updater.mqtt_client)
    pieces = [json.loads(payload)["data"] for _, payload in recorder.published[1:]]
    assert b"".join(base64.b64decode(d) for d in pieces) == firmware


def test_device_nack_aborts_transfer(tmp_path: Path) -> None:
    updater = _make_updater(tmp_path, b"project_e2e\x00" + b"A" * 250)
    _run_simulated_transfer(updater, ack=False)
    assert updater.state == ota.TransferState.ERROR


# --- FileTransfer end-to-end: every content source over the simulated wire ---
# These guard the switch from concrete per-device files to rendered / inline
# content: whatever the provider yields must arrive byte-identically.

def _transferred_payload(entry: "ota.FileEntry", device: "ota.DeviceEntry",
                         manager: "ota.ConfigManager") -> bytes:
    """Run a full simulated FileTransfer for `entry`; return the reassembled bytes
    after verifying the start message (device path, size, md5) against them."""
    provider = ota.build_file_provider(entry, device, manager)
    transfer = ota.FileTransfer(
        ota.DeviceConfig(mac_address=device.mac, project_name="x"),
        ota.MQTTConfig(host="broker"),
        entry,
        provider,
    )
    transfer.mqtt_client = _RecordingMQTT()  # type: ignore  # deliberate test double for the MQTT client
    _run_simulated_transfer(transfer)
    assert transfer.state == ota.TransferState.DONE

    recorder = cast(_RecordingMQTT, transfer.mqtt_client)
    start = json.loads(recorder.published[0][1])
    payload = b"".join(base64.b64decode(json.loads(p)["data"]) for _, p in recorder.published[1:])
    assert start["name"] == entry.device_path
    assert start["fileSize"] == len(payload)
    assert start["md5"] == hashlib.md5(payload).hexdigest()
    return payload


def test_e2e_local_file_arrives_byte_identical(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    cert = bytes(range(256)) * 11  # 2816 bytes, binary, multi-piece like the real CA cert
    entry = ota.FileEntry(name="CA cert", device_path="/config/mosq-ca.crt",
                          local_path=_write(tmp_path, "ca.crt", cert))
    assert _transferred_payload(entry, device, manager) == cert


def test_e2e_rendered_server_json_arrives_valid(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="Server config", device_path="/config/server.json",
                          render="server_json")
    parsed = json.loads(_transferred_payload(entry, device, manager))
    assert parsed == {
        "mqttUserName": "dev",
        "mqttPassword": "secret",
        "mqttServerUrl": "broker.example.com",
        "mqttServerPort": 8883,
        "haDiscovery": True,
    }


def test_e2e_inline_content_arrives_compact(tmp_path: Path) -> None:
    device, manager = _device_with_secrets(tmp_path)
    entry = ota.FileEntry(name="Tube config", device_path="/config/tube.json",
                          content={"tube": 1})
    assert _transferred_payload(entry, device, manager) == b'{"tube":1}'


# --- The real devices.yaml: every file entry must still be sendable ----------

def test_real_devices_yaml_entries_resolve(tmp_path: Path) -> None:
    """Load the repo's tracked devices.yaml and check every device: the
    server.json renders from (synthetic) secrets, inline content serializes,
    and local_path entries resolve under ota/. Git-ignored / build-output
    files may be absent on a fresh clone, so only their paths are checked."""
    ota_dir = Path(ota.__file__).resolve().parent
    projects = ota.DeviceManager(str(ota_dir / "otaUpdate.py")).load()
    devices = [(p, d) for p in projects for d in p.devices]
    assert devices, "devices.yaml lists no devices"

    macs = {d.mac for _, d in devices}
    manager = _write_secrets(tmp_path, (
        "server_defaults:\n"
        "  mqttServerUrl: broker.example.com\n"
        "  mqttServerPort: 8883\n"
        "devices:\n"
        + "".join(f"  {mac}:\n    mqttUserName: u\n    mqttPassword: p\n" for mac in sorted(macs))
    ))

    for _project, device in devices:
        assert any(f.render == "server_json" for f in device.files), \
            f"{device.mac} has no rendered server.json entry"
        for entry in device.files:
            if entry.local_path is not None:
                assert not entry.local_path.is_absolute() or str(entry.local_path).startswith(str(ota_dir.parent)), \
                    f"{device.mac} {entry.name}: path escapes the repo"
                if not entry.local_path.exists():
                    continue  # git-ignored cert / firmware build output — absent on a fresh clone
            payload = _transferred_payload(entry, device, manager)
            assert payload
            if entry.device_path.endswith(".json"):
                json.loads(payload)  # every JSON config must arrive parseable
