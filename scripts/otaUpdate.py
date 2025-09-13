#!/usr/bin/env python3
"""
ESP8266/ESP32 Over-The-Air (OTA) Update Tool via MQTT

This tool provides a modular approach to updating ESP devices firmware
through MQTT communication with proper error handling and progress tracking.
"""

import json
import os
import time
import zlib
import base64
import sys
import signal
import enum
import logging
from pathlib import Path
from typing import Optional, Tuple, Dict, Any
from dataclasses import dataclass
from tqdm import tqdm
import subprocess

# Try to import paho-mqtt, install if not available
try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
    print("Paho MQTT library not found. Installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "paho-mqtt"])
    import paho.mqtt.client as mqtt


class OTAState(enum.Enum):
    """States for the OTA update process"""
    IDLE = 0
    WAIT_START_ACK = 1
    SENDING_FW = 2
    WAIT_PIECE_ACK = 3
    WAIT_CHECK_ACK = 4
    DONE = 5
    ERROR = 6


@dataclass
class MQTTConfig:
    """Configuration data for MQTT connection"""
    username: str
    password: str
    server_url: str
    server_port: int
    ca_cert_path: str
    client_id: str = 'Python_OTA'


@dataclass
class DeviceConfig:
    """Configuration data for the target device"""
    mac_address: str
    project_name: str

    @property
    def send_topic(self) -> str:
        return f'iot/stod/{self.mac_address}/common'

    @property
    def receive_topic(self) -> str:
        return f'iot/dtos/{self.mac_address}/common'


class ConfigManager:
    """Manages configuration loading and validation"""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.parent_dir = self.script_dir.parent
        self.data_dir = self.parent_dir / 'data' / 'config'

    def load_mqtt_config(self) -> MQTTConfig:
        """Load MQTT configuration from server.json"""
        server_json_path = self.data_dir / 'server.json'

        if not server_json_path.exists():
            raise FileNotFoundError(f"Configuration file not found: {server_json_path}")

        with open(server_json_path, 'r') as file:
            credentials = json.load(file)

        ca_cert_path = self.data_dir / 'mosq-ca.crt'
        if not ca_cert_path.exists():
            raise FileNotFoundError(f"CA certificate not found: {ca_cert_path}")

        return MQTTConfig(
            username=credentials['mqttUserName'],
            password=credentials['mqttPassword'],
            server_url=credentials['mqttServerUrl'],
            server_port=credentials['mqttServerPort'],
            ca_cert_path=str(ca_cert_path)
        )

    def get_firmware_path(self, project_name: str) -> Path:
        """Get the firmware binary path"""
        firmware_path = self.parent_dir / '.pio' / 'build' / project_name / 'firmware.bin'

        if not firmware_path.exists():
            raise FileNotFoundError(f"Firmware file not found: {firmware_path}")

        return firmware_path


class FirmwareManager:
    """Handles firmware file operations and validation"""

    def __init__(self, firmware_path: Path):
        self.firmware_path = firmware_path
        self._firmware_data: Optional[bytes] = None
        self._crc32: Optional[int] = None
        self._firmware_id: Optional[str] = None

    @property
    def firmware_data(self) -> bytes:
        """Lazy loading of firmware data"""
        if self._firmware_data is None:
            self._firmware_data = self._read_firmware()
        return self._firmware_data

    @property
    def size(self) -> int:
        """Get firmware size"""
        return len(self.firmware_data)

    @property
    def crc32(self) -> int:
        """Calculate and cache CRC32 checksum"""
        if self._crc32 is None:
            self._crc32 = zlib.crc32(self.firmware_data) & 0xFFFFFFFF
        return self._crc32

    @property
    def firmware_id(self) -> str:
        """Extract and cache firmware ID"""
        if self._firmware_id is None:
            success, fw_id = self._extract_firmware_id()
            if not success:
                raise ValueError("Could not extract firmware ID from binary")
            self._firmware_id = fw_id
        return self._firmware_id

    def _read_firmware(self) -> bytes:
        """Read firmware binary file"""
        try:
            with open(self.firmware_path, 'rb') as f:
                return f.read()
        except IOError as e:
            raise IOError(f"Failed to read firmware file: {e}")

    def _extract_firmware_id(self) -> Tuple[bool, str]:
        """Extract firmware identifier from binary"""
        begin_of_id = b"project_"
        start_index = self.firmware_data.find(begin_of_id)

        if start_index != -1:
            end_index = self.firmware_data.find(b'\0', start_index + len(begin_of_id))
            if end_index != -1:
                identifier = self.firmware_data[start_index:end_index].decode('utf-8')
                logging.info(f"Firmware ID found: \"{identifier}\"")
                return True, identifier

        logging.error("Firmware ID not found in binary")
        return False, ""


class MQTTClient:
    """MQTT client wrapper with connection management"""

    def __init__(self, config: MQTTConfig):
        self.config = config
        self.client = mqtt.Client(client_id=config.client_id)
        self.client.username_pw_set(username=config.username, password=config.password)
        self.client.tls_set(ca_certs=config.ca_cert_path)
        self._connected = False

    def set_callbacks(self, on_connect_callback, on_message_callback):
        """Set MQTT event callbacks"""
        self.client.on_connect = on_connect_callback
        self.client.on_message = on_message_callback

    def connect(self) -> bool:
        """Connect to MQTT broker"""
        try:
            self.client.connect(self.config.server_url, self.config.server_port, 60)
            return True
        except Exception as e:
            logging.error(f"Failed to connect to MQTT broker: {e}")
            return False

    def subscribe(self, topic: str):
        """Subscribe to MQTT topic"""
        self.client.subscribe(topic)

    def publish(self, topic: str, payload: str):
        """Publish message to MQTT topic"""
        self.client.publish(topic, payload)

    def loop(self, timeout: float = 0.1):
        """Process MQTT network events"""
        self.client.loop(timeout=timeout)

    def disconnect(self):
        """Disconnect from MQTT broker"""
        self.client.disconnect()
        self.client.loop_stop()


class OTAUpdater:
    """Main OTA update orchestrator"""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, firmware_path: Path):
        self.device_config = device_config
        self.firmware_manager = FirmwareManager(firmware_path)
        self.mqtt_client = MQTTClient(mqtt_config)

        # OTA state management
        self.state = OTAState.IDLE
        self.piece_number = 0
        self.remaining_bytes = 0
        self.timer_start = 0
        self.piece_size = 100
        self.timeout_seconds = 25
        self.progress_bar: Optional[tqdm] = None

        # Set up MQTT callbacks
        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

        # Set up logging
        logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

    def _on_connect(self, client, userdata, flags, rc):
        """Handle MQTT connection event"""
        if rc == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._send_start_message()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {rc}")
            self.state = OTAState.ERROR

    def _on_message(self, client, userdata, msg):
        """Handle incoming MQTT messages"""
        try:
            message = json.loads(msg.payload.decode())
            self._process_ota_response(message)
        except json.JSONDecodeError as e:
            logging.error(f"Failed to parse MQTT message: {e}")
            self.state = OTAState.ERROR

    def _send_start_message(self):
        """Send OTA start message to device"""
        start_message = {
            "fileSize": self.firmware_manager.size,
            "crc32": self.firmware_manager.crc32,
            "binId": self.firmware_manager.firmware_id
        }

        self.mqtt_client.publish(self.device_config.send_topic, json.dumps(start_message))
        logging.info(f"OTA started - Size: {self.firmware_manager.size} bytes, CRC32: {self.firmware_manager.crc32}")

        self.state = OTAState.WAIT_START_ACK
        self.remaining_bytes = self.firmware_manager.size
        self.timer_start = time.time()

    def _process_ota_response(self, message: Dict[str, Any]):
        """Process OTA response messages from device"""
        if "type" not in message:
            logging.warning("Received message without 'type' field")
            return

        ack = message["type"] != 0

        if self.state == OTAState.WAIT_START_ACK and ack:
            logging.info("Start acknowledgment received, beginning firmware transfer")
            self.progress_bar = tqdm(
                total=self.firmware_manager.size,
                desc="Sending Firmware",
                unit="B",
                unit_scale=True
            )
            self.state = OTAState.SENDING_FW

        elif self.state == OTAState.WAIT_PIECE_ACK and ack:
            self.state = OTAState.SENDING_FW

        elif self.state == OTAState.WAIT_CHECK_ACK and ack:
            self.state = OTAState.DONE
            logging.info("OTA update completed successfully!")

        else:
            logging.error(f"Received negative acknowledgment or unexpected state. Message: {message}")
            self.state = OTAState.ERROR

    def _send_firmware_piece(self):
        """Send a piece of firmware to the device"""
        offset = self.firmware_manager.size - self.remaining_bytes
        read_size = min(self.remaining_bytes, self.piece_size)
        data = self.firmware_manager.firmware_data[offset:offset + read_size]

        piece_message = {
            "piece": self.piece_number,
            "data": base64.b64encode(data).decode('utf-8')
        }

        self.mqtt_client.publish(self.device_config.send_topic, json.dumps(piece_message))

        self.state = OTAState.WAIT_PIECE_ACK
        self.timer_start = time.time()
        self.piece_number += 1
        self.remaining_bytes -= read_size

        if self.progress_bar:
            self.progress_bar.update(read_size)

    def _process_state(self):
        """Process current OTA state"""
        current_time = time.time()

        if self.state == OTAState.SENDING_FW:
            if self.remaining_bytes > 0:
                self._send_firmware_piece()
            else:
                logging.info("All firmware pieces sent, waiting for final verification")
                self.state = OTAState.WAIT_CHECK_ACK
                self.timer_start = current_time

        elif self.state in {OTAState.WAIT_START_ACK, OTAState.WAIT_PIECE_ACK, OTAState.WAIT_CHECK_ACK}:
            if current_time - self.timer_start > self.timeout_seconds:
                logging.error(f"Timeout occurred in state: {self.state.name}")
                self.state = OTAState.ERROR

    def _cleanup(self):
        """Clean up resources"""
        if self.progress_bar:
            self.progress_bar.close()
        self.mqtt_client.disconnect()

    def run(self) -> bool:
        """Run the OTA update process"""
        if not self.mqtt_client.connect():
            return False

        try:
            while self.state not in {OTAState.DONE, OTAState.ERROR}:
                self.mqtt_client.loop(timeout=0.1)
                self._process_state()

            success = self.state == OTAState.DONE
            if success:
                logging.info("OTA update completed successfully")
            else:
                logging.error("OTA update failed")

            return success

        except KeyboardInterrupt:
            logging.info("OTA update interrupted by user")
            return False
        except Exception as e:
            logging.error(f"Unexpected error during OTA update: {e}")
            return False
        finally:
            self._cleanup()


def main():
    """Main entry point"""
    # Configuration
    device_config = DeviceConfig(
        mac_address="bcddc2b622c9",
        project_name="project_esp8266_smoke"
    )

    try:
        # Load configuration
        config_manager = ConfigManager(__file__)
        mqtt_config = config_manager.load_mqtt_config()
        firmware_path = config_manager.get_firmware_path(device_config.project_name)

        # Create and run OTA updater
        ota_updater = OTAUpdater(device_config, mqtt_config, firmware_path)

        # Set up signal handler for graceful shutdown
        def signal_handler(sig, frame):
            logging.info("Received interrupt signal, shutting down...")
            ota_updater._cleanup()
            sys.exit(0)

        signal.signal(signal.SIGINT, signal_handler)

        # Run the update
        success = ota_updater.run()
        sys.exit(0 if success else 1)

    except FileNotFoundError as e:
        logging.error(f"Configuration error: {e}")
        sys.exit(1)
    except ValueError as e:
        logging.error(f"Firmware error: {e}")
        sys.exit(1)
    except Exception as e:
        logging.error(f"Unexpected error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()