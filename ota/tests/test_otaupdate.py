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


def _run_simulated_transfer(updater: "ota.OTAUpdater", *, ack: bool = True, max_ticks: int = 10000) -> None:
    """Drive the sender to completion with a simulated device that ACKs/NACKs every wait."""
    waits = {
        ota.TransferState.WAIT_START_ACK,
        ota.TransferState.WAIT_PIECE_ACK,
        ota.TransferState.WAIT_CHECK_ACK,
    }
    updater._send_start_message()
    ticks = 0
    while updater.state not in (ota.TransferState.DONE, ota.TransferState.ERROR):
        ticks += 1
        assert ticks < max_ticks, "state machine did not terminate"
        if updater.state in waits:
            updater._pending_messages.append({"type": 1 if ack else 0})
        updater._process_state()


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
