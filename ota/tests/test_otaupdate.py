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
from pathlib import Path

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


# --- MQTTConfig: default port resolution and validation --------------------

@pytest.mark.parametrize("protocol, tls, expected_port", [
    ("mqtt", False, 1883),
    ("mqtt", True, 8883),
    ("ws", False, 80),
    ("ws", True, 443),
])
def test_mqttconfig_default_port(protocol, tls, expected_port):
    assert ota.MQTTConfig(protocol=protocol, host="broker", tls_enabled=tls).port == expected_port


def test_mqttconfig_explicit_port_kept():
    assert ota.MQTTConfig(host="broker", port=12345).port == 12345


def test_mqttconfig_autogenerates_client_id():
    assert ota.MQTTConfig(host="broker").client_id.startswith("Python_OTA_")


def test_mqttconfig_bad_protocol():
    with pytest.raises(ValueError):
        ota.MQTTConfig(protocol="amqp", host="broker")


def test_mqttconfig_port_out_of_range():
    with pytest.raises(ValueError):
        ota.MQTTConfig(host="broker", port=70000)


def test_mqttconfig_empty_host():
    with pytest.raises(ValueError):
        ota.MQTTConfig(host="   ")


# --- DeviceConfig: topic construction --------------------------------------

def test_deviceconfig_topics():
    device = ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x")
    assert device.send_topic == "iot/stod/AABBCCDDEEFF/common"
    assert device.receive_topic == "iot/dtos/AABBCCDDEEFF/common"


# --- FirmwareManager: size, md5, firmware id -------------------------------

def test_firmware_size_and_md5(tmp_path):
    blob = b"\x01\x02project_esp32_can\x00trailing"
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", blob))
    assert firmware.size == len(blob)
    assert firmware.md5 == hashlib.md5(blob).hexdigest()


def test_firmware_id_extracted(tmp_path):
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", b"....project_esp8266_thermo\x00...."))
    assert firmware.firmware_id == "project_esp8266_thermo"


def test_firmware_id_missing_raises(tmp_path):
    firmware = ota.FirmwareManager(_write(tmp_path, "firmware.bin", b"no id here"))
    with pytest.raises(ValueError):
        _ = firmware.firmware_id


# --- FileDataProvider: JSON minification + comment stripping ---------------

def test_json_is_serialized_compact(tmp_path):
    src = '{\n  "a": 1,\n  "b": [1, 2, 3]\n}\n'
    provider = ota.FileDataProvider(_write(tmp_path, "config.json", src.encode()))
    assert provider.data == b'{"a":1,"b":[1,2,3]}'


def test_json_comments_stripped(tmp_path):
    src = '{\n  // line comment\n  "a": 1, /* block */ "b": "//not a comment"\n}'
    provider = ota.FileDataProvider(_write(tmp_path, "config.json", src.encode()))
    assert provider.data == b'{"a":1,"b":"//not a comment"}'


def test_non_json_passthrough(tmp_path):
    blob = b"\x00\x01raw bytes\xff"
    provider = ota.FileDataProvider(_write(tmp_path, "blob.bin", blob))
    assert provider.data == blob


def test_invalid_json_raises(tmp_path):
    provider = ota.FileDataProvider(_write(tmp_path, "bad.json", b"{not valid"))
    with pytest.raises(ValueError):
        _ = provider.data


# --- OTA framing: the piece-splitting that builds the wire frames -----------

class _RecordingMQTT:
    """Stands in for MQTTClient: records publish(topic, payload) instead of networking."""

    def __init__(self):
        self.published = []

    def publish(self, topic, payload):
        self.published.append((topic, payload))


def _make_updater(tmp_path: Path, firmware: bytes) -> "ota.OTAUpdater":
    device = ota.DeviceConfig(mac_address="AABBCCDDEEFF", project_name="x")
    updater = ota.OTAUpdater(device, ota.MQTTConfig(host="broker"), _write(tmp_path, "firmware.bin", firmware))
    updater.mqtt_client = _RecordingMQTT()
    return updater


def _drain_pieces(updater: "ota.OTAUpdater") -> list:
    """Drive _send_piece until every byte is sent; return the decoded piece payloads."""
    updater.remaining_bytes = updater.size
    pieces = []
    while updater.remaining_bytes > 0:
        index = len(updater.mqtt_client.published)
        updater._send_piece()
        topic, payload = updater.mqtt_client.published[index]
        assert topic == updater.device_config.send_topic
        pieces.append(json.loads(payload))
    return pieces


def test_piece_splitting_reconstructs_firmware(tmp_path):
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


def test_exact_multiple_of_piece_size(tmp_path):
    updater = _make_updater(tmp_path, b"A" * 200)  # exactly two full pieces, no remainder
    pieces = _drain_pieces(updater)
    assert len(pieces) == 2
    assert all(len(base64.b64decode(p["data"])) == 100 for p in pieces)


def test_single_short_piece(tmp_path):
    updater = _make_updater(tmp_path, b"hello")  # smaller than piece_size -> one piece
    pieces = _drain_pieces(updater)
    assert len(pieces) == 1
    assert base64.b64decode(pieces[0]["data"]) == b"hello"


# --- OTA start message -----------------------------------------------------

def test_start_message_fields(tmp_path):
    firmware = b"....project_esp32_can\x00...."
    message = _make_updater(tmp_path, firmware)._build_start_message()
    assert message["name"] == "espFirmware"
    assert message["fileSize"] == len(firmware)
    assert message["binId"] == "project_esp32_can"
    assert len(message["md5"]) == 32
