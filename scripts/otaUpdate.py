#!/usr/bin/env python3
"""
ESP8266/ESP32 Over-The-Air (OTA) Update Tool via MQTT

This tool provides a modular approach to updating ESP devices firmware
through MQTT communication with proper error handling and progress tracking.
Uses YAML configuration file (config.yaml) instead of JSON.
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
import uuid
import hashlib
from pathlib import Path
from typing import Optional, Tuple, Dict, Any, Union
from dataclasses import dataclass
from tqdm import tqdm
import subprocess

# Try to import required libraries, install if not available
try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
    print("Paho MQTT library not found. Installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "paho-mqtt"])
    import paho.mqtt.client as mqtt

try:
    import yaml
except ModuleNotFoundError:
    print("PyYAML library not found. Installing...")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyyaml"])
    import yaml


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
    protocol: str = "mqtt"                    # "mqtt" or "ws"
    host: str = ""                           # Server hostname or IP
    port: int = 0                            # Server port (auto-determined if None)
    basepath: str = "/"                      # Only used with WebSocket
    client_id: str = ""                      # Unique client ID (auto-generated if None)
    username: Optional[str] = None           # MQTT username
    password: Optional[str] = None           # MQTT password
    tls_enabled: bool = False                # Use TLS encryption
    cafile: Optional[str] = None             # CA certificate file path

    def __post_init__(self):
        """Validate and set defaults after initialization"""
        # Validate protocol
        if self.protocol not in ["mqtt", "ws"]:
            raise ValueError(f"Unsupported protocol: {self.protocol}. Must be 'mqtt' or 'ws'")

        # Set default port based on protocol and TLS
        if self.port is None:
            if self.protocol == "mqtt":
                self.port = 8883 if self.tls_enabled else 1883
            else:  # ws
                self.port = 443 if self.tls_enabled else 80

        # Validate port range
        if not (1 <= self.port <= 65535):
            raise ValueError(f"Invalid port: {self.port}. Must be between 1 and 65535")

        # Generate random client ID if not provided
        if self.client_id is None:
            self.client_id = f"Python_OTA_{uuid.uuid4().hex[:8]}"

        # Validate CA file path if provided (handle Windows paths correctly)
        if self.cafile is not None:
            ca_path = Path(self.cafile).expanduser().resolve()
            if not ca_path.exists():
                raise FileNotFoundError(f"CA certificate file not found: {ca_path}")
            # Update with resolved path for consistency
            self.cafile = str(ca_path)

        # Validate host is provided
        if not self.host.strip():
            raise ValueError("Host is required and cannot be empty")


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
    """Manages YAML configuration loading and validation"""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.parent_dir = self.script_dir.parent
        self.config_file = self.script_dir / 'config.yaml'

    def load_mqtt_config(self) -> MQTTConfig:
        """Load MQTT configuration from config.yaml"""
        if not self.config_file.exists():
            raise FileNotFoundError(
                f"Configuration file not found: {self.config_file}\n"
                f"Please create a config.yaml file in the same directory as the script."
            )

        try:
            with open(self.config_file, 'r', encoding='utf-8') as file:
                config_data = yaml.safe_load(file)
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse YAML configuration file: {e}")
        except UnicodeDecodeError as e:
            raise ValueError(f"Configuration file encoding error. Please ensure the file is UTF-8 encoded: {e}")
        except Exception as e:
            raise IOError(f"Failed to read configuration file: {e}")

        if config_data is None:
            config_data = {}

        # Extract configuration values with defaults
        try:
            mqtt_config = MQTTConfig(
                protocol=config_data.get('protocol', 'mqtt'),
                host=config_data.get('host', ''),
                port=config_data.get('port', 0),
                basepath=config_data.get('basepath', '/'),
                client_id=config_data.get('client_id', "OtaUpdater"),
                username=config_data.get('username'),
                password=config_data.get('password'),
                tls_enabled=config_data.get('tls_enabled', False),
                cafile=config_data.get('cafile')
            )
            return mqtt_config
        except (ValueError, FileNotFoundError) as e:
            raise ValueError(f"Configuration validation error: {e}")

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
        self._md5: Optional[str] = None
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
    def md5(self) -> str:
        """Calculate and cache MD5 hash"""
        if self._md5 is None:
            md5_hash = hashlib.md5()
            md5_hash.update(self.firmware_data)
            self._md5 = md5_hash.hexdigest()
        return self._md5

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
        self._setup_client()
        self._connected = False

    def _setup_client(self):
        """Set up MQTT client based on configuration"""
        if self.config.protocol == "mqtt":
            self.client = mqtt.Client(client_id=self.config.client_id)
        elif self.config.protocol == "ws":
            self.client = mqtt.Client(
                client_id=self.config.client_id,
                transport="websockets"
            )
            # Set WebSocket path
            if hasattr(self.client, 'ws_set_options'):
                self.client.ws_set_options(path=self.config.basepath)
        else:
            raise ValueError(f"Unsupported protocol: {self.config.protocol}")

        # Set authentication if provided
        if self.config.username is not None and self.config.password is not None:
            self.client.username_pw_set(
                username=self.config.username,
                password=self.config.password
            )

        # Set up TLS if enabled
        if self.config.tls_enabled:
            if self.config.cafile:
                # Use provided CA certificate file
                self.client.tls_set(ca_certs=self.config.cafile)
            else:
                # Use system default certificate store
                self.client.tls_set()

    def set_callbacks(self, on_connect_callback, on_message_callback):
        """Set MQTT event callbacks"""
        self.client.on_connect = on_connect_callback
        self.client.on_message = on_message_callback

    def connect(self) -> bool:
        """Connect to MQTT broker"""
        try:
            logging.info(f"Connecting to {self.config.protocol.upper()} broker at {self.config.host}:{self.config.port}")
            if self.config.tls_enabled:
                logging.info("Using TLS encryption")

            self.client.connect(self.config.host, self.config.port, 60)
            return True
        except Exception as e:
            logging.error(f"Failed to connect to MQTT broker: {e}")
            return False

    def subscribe(self, topic: str):
        """Subscribe to MQTT topic"""
        self.client.subscribe(topic)
        logging.info(f"Subscribed to topic: {topic}")

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
            "md5": self.firmware_manager.md5,
            "binId": self.firmware_manager.firmware_id
        }

        self.mqtt_client.publish(self.device_config.send_topic, json.dumps(start_message))
        logging.info(f"OTA started - Size: {self.firmware_manager.size} bytes")
        logging.info(f"  CRC32: {self.firmware_manager.crc32}")
        logging.info(f"  MD5:   {self.firmware_manager.md5}")

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

    def _close_progress_bar(self):
        """Close and clean up the progress bar"""
        if self.progress_bar:
            self.progress_bar.close()
            self.progress_bar = None

    def _process_state(self):
        """Process current OTA state"""
        current_time = time.time()

        if self.state == OTAState.SENDING_FW:
            if self.remaining_bytes > 0:
                self._send_firmware_piece()
            else:
                # Close progress bar immediately when transfer is complete
                self._close_progress_bar()
                logging.info("All firmware pieces sent, waiting for final verification")
                self.state = OTAState.WAIT_CHECK_ACK
                self.timer_start = current_time

        elif self.state in {OTAState.WAIT_START_ACK, OTAState.WAIT_PIECE_ACK, OTAState.WAIT_CHECK_ACK}:
            if current_time - self.timer_start > self.timeout_seconds:
                logging.error(f"Timeout occurred in state: {self.state.name}")
                self.state = OTAState.ERROR

    def _cleanup(self):
        """Clean up resources"""
        self._close_progress_bar()
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
    # Device configuration - modify these values for your device
    device_config = DeviceConfig(
        mac_address="bcddc2b622c9",
        project_name="project_esp8266_smoke"
    )

    try:
        # Load configuration from config.yaml
        config_manager = ConfigManager(__file__)

        print("Loading MQTT configuration from config.yaml...")
        mqtt_config = config_manager.load_mqtt_config()

        print(f"✅ Configuration loaded successfully:")
        print(f"  Protocol: {mqtt_config.protocol}")
        print(f"  Host: {mqtt_config.host}")
        print(f"  Port: {mqtt_config.port}")
        print(f"  Client ID: {mqtt_config.client_id}")
        print(f"  TLS Enabled: {mqtt_config.tls_enabled}")
        print(f"  Authentication: {'Yes' if mqtt_config.username else 'No'}")

        # Get firmware path
        firmware_path = config_manager.get_firmware_path(device_config.project_name)
        print(f"  Firmware path: {firmware_path}")
        print()

        # Create and run OTA updater
        ota_updater = OTAUpdater(device_config, mqtt_config, firmware_path)

        # Set up signal handler for graceful shutdown
        def signal_handler(sig, frame):
            logging.info("Received interrupt signal, shutting down...")
            ota_updater._cleanup()
            sys.exit(0)

        # Register signal handlers (SIGINT works on both Windows and Unix)
        signal.signal(signal.SIGINT, signal_handler)

        # On Unix systems, also handle SIGTERM
        if hasattr(signal, 'SIGTERM'):
            signal.signal(signal.SIGTERM, signal_handler)

        # Run the update
        success = ota_updater.run()
        sys.exit(0 if success else 1)

    except FileNotFoundError as e:
        print(f"❌ File Error: {e}")
        print("\nTo fix this issue:")
        print("1. Create a 'config.yaml' file in the same directory as this script")
        print("2. Use the example configuration provided in the documentation")
        print("3. Make sure the firmware binary exists in the PlatformIO build directory")
        sys.exit(1)
    except ValueError as e:
        print(f"❌ Configuration Error: {e}")
        print("\nPlease check your config.yaml file for:")
        print("- Correct YAML syntax")
        print("- Valid protocol ('mqtt' or 'ws')")
        print("- Valid host address")
        print("- Valid port number (1-65535)")
        print("- Correct file paths (if using cafile)")
        sys.exit(1)
    except yaml.YAMLError as e:
        print(f"❌ YAML Parsing Error: {e}")
        print("\nYour config.yaml file has invalid YAML syntax.")
        print("Please check for:")
        print("- Proper indentation (use spaces, not tabs)")
        print("- Correct colon placement (key: value)")
        print("- Proper quoting of strings")
        sys.exit(1)
    except Exception as e:
        logging.error(f"Unexpected error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()