#!/usr/bin/env python3
"""
ESP8266/ESP32 Over-The-Air (OTA) Update Tool via MQTT

This tool provides a modular approach to updating ESP devices firmware
and transferring configuration files through MQTT communication with
proper error handling and progress tracking.
Uses YAML configuration file (config.yaml) and device list (devices.yaml).
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
import curses
from pathlib import Path
from typing import Optional, Tuple, Dict, Any, List
from dataclasses import dataclass, field
from tqdm import tqdm
import yaml
import paho.mqtt.client as mqtt


# ---------------------------------------------------------------------------
# Enums & Data classes
# ---------------------------------------------------------------------------

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
class FileEntry:
    """A transferable file entry from devices.yaml"""
    name: str                  # Display name shown in the menu
    local_path: Path           # Local path to the file (relative to ota/ directory)
    device_path: str           # Destination path on the device (sent as 'name' in JSON)


@dataclass
class DeviceEntry:
    """A single device entry from devices.yaml"""
    mac: str
    friendly_name: Optional[str] = None
    files: List[FileEntry] = field(default_factory=list)

    @property
    def display_name(self) -> str:
        if self.friendly_name:
            return f"{self.friendly_name}  ({self.mac})"
        return self.mac


@dataclass
class ProjectEntry:
    """A project entry from devices.yaml"""
    name: str
    pio_project: str
    devices: List[DeviceEntry] = field(default_factory=list)


@dataclass
class DeviceConfig:
    """Configuration data for the target device (used by OTAUpdater and FileTransfer)"""
    mac_address: str
    project_name: str

    @property
    def send_topic(self) -> str:
        return f'iot/stod/{self.mac_address}/common'

    @property
    def receive_topic(self) -> str:
        return f'iot/dtos/{self.mac_address}/common'


# ---------------------------------------------------------------------------
# Device list manager
# ---------------------------------------------------------------------------

class DeviceManager:
    """Loads and provides access to the devices.yaml device list"""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.devices_file = self.script_dir / 'devices.yaml'

    def load(self) -> List[ProjectEntry]:
        if not self.devices_file.exists():
            raise FileNotFoundError(
                f"Device list file not found: {self.devices_file}\n"
                f"Please create a devices.yaml file in the same directory as the script."
            )

        try:
            with open(self.devices_file, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse devices.yaml: {e}")

        if not data or 'projects' not in data:
            raise ValueError("devices.yaml must contain a 'projects' key")

        projects: List[ProjectEntry] = []
        for p in data['projects']:
            if 'name' not in p or 'pio_project' not in p:
                raise ValueError("Each project entry must have 'name' and 'pio_project' fields")

            devices: List[DeviceEntry] = []
            for d in p.get('devices', []):
                if 'mac' not in d:
                    raise ValueError(f"Each device entry must have a 'mac' field (project: {p['name']})")

                # Parse optional file entries for this device
                files: List[FileEntry] = []
                for f in d.get('files', []):
                    if 'name' not in f or 'local_path' not in f or 'device_path' not in f:
                        raise ValueError(
                            f"Each file entry must have 'name', 'local_path' and 'device_path' fields "
                            f"(device: {d['mac']})"
                        )
                    files.append(FileEntry(
                        name=f['name'],
                        local_path=self.script_dir / f['local_path'],
                        device_path=f['device_path']
                    ))

                devices.append(DeviceEntry(
                    mac=d['mac'],
                    friendly_name=d.get('friendly_name'),
                    files=files
                ))

            projects.append(ProjectEntry(
                name=p['name'],
                pio_project=p['pio_project'],
                devices=devices
            ))

        if not projects:
            raise ValueError("devices.yaml contains no projects")

        return projects


# ---------------------------------------------------------------------------
# Interactive curses menu
# ---------------------------------------------------------------------------

class MenuSelector:
    """Arrow-key driven interactive menu using curses"""

    # Return sentinels
    BACK = "__BACK__"
    CANCEL = "__CANCEL__"

    def select(self, title: str, options: List[str], show_back: bool = False) -> str | None:
        """
        Display an interactive menu and return the selected option string,
        MenuSelector.BACK, or MenuSelector.CANCEL.
        """
        result = curses.wrapper(self._run, title, options, show_back)
        return result

    def _run(self, stdscr, title: str, options: List[str], show_back: bool):
        curses.curs_set(0)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)   # selected item
        curses.init_pair(2, curses.COLOR_CYAN,  -1)                  # title
        curses.init_pair(3, curses.COLOR_YELLOW, -1)                 # hint line

        # Build full item list: real options + navigation entries
        nav_items = []
        if show_back:
            nav_items.append("← Back")
        nav_items.append("✕ Cancel")

        all_items = options + nav_items
        current = 0

        while True:
            stdscr.clear()
            height, width = stdscr.getmaxyx()

            # Title
            stdscr.attron(curses.color_pair(2) | curses.A_BOLD)
            stdscr.addstr(1, 2, title[:width - 4])
            stdscr.attroff(curses.color_pair(2) | curses.A_BOLD)

            # Separator
            stdscr.addstr(2, 2, "─" * min(len(title) + 2, width - 4))

            # Items
            for idx, item in enumerate(all_items):
                row = 4 + idx
                if row >= height - 2:
                    break
                is_nav = idx >= len(options)
                prefix = "  "
                if idx == current:
                    stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
                    stdscr.addstr(row, 2, f" {prefix}{item} ".ljust(width - 4)[:width - 4])
                    stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)
                else:
                    if is_nav:
                        stdscr.attron(curses.A_DIM)
                    stdscr.addstr(row, 2, f" {prefix}{item}"[:width - 4])
                    if is_nav:
                        stdscr.attroff(curses.A_DIM)

            # Hint
            hint = "↑↓ Navigate   Enter Select   Esc Cancel"
            stdscr.attron(curses.color_pair(3))
            stdscr.addstr(height - 1, 2, hint[:width - 4])
            stdscr.attroff(curses.color_pair(3))

            stdscr.refresh()
            key = stdscr.getch()

            if key == curses.KEY_UP:
                current = (current - 1) % len(all_items)
            elif key == curses.KEY_DOWN:
                current = (current + 1) % len(all_items)
            elif key in (curses.KEY_ENTER, ord('\n'), ord('\r')):
                selected = all_items[current]
                if selected == "✕ Cancel":
                    return self.CANCEL
                if selected == "← Back":
                    return self.BACK
                return selected
            elif key == 27:  # Escape
                return self.CANCEL


# ---------------------------------------------------------------------------
# Config manager
# ---------------------------------------------------------------------------

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
            raise ValueError(f"Configuration file encoding error: {e}")
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

    def get_firmware_path(self, pio_project: str) -> Path:
        """Get the firmware binary path"""
        firmware_path = self.parent_dir / '.pio' / 'build' / pio_project / 'firmware.bin'

        if not firmware_path.exists():
            raise FileNotFoundError(f"Firmware file not found: {firmware_path}")

        return firmware_path


# ---------------------------------------------------------------------------
# Firmware manager
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# Generic file data provider (used by FileTransfer)
# ---------------------------------------------------------------------------

class FileDataProvider:
    """Reads an arbitrary binary file and provides size and checksum properties"""

    def __init__(self, file_path: Path):
        self.file_path = file_path
        self._data: Optional[bytes] = None
        self._crc32: Optional[int] = None
        self._md5: Optional[str] = None

    @property
    def data(self) -> bytes:
        """Lazy loading of file data"""
        if self._data is None:
            try:
                with open(self.file_path, 'rb') as f:
                    self._data = f.read()
            except IOError as e:
                raise IOError(f"Failed to read file: {e}")
        return self._data

    @property
    def size(self) -> int:
        """Get file size"""
        return len(self.data)

    @property
    def crc32(self) -> int:
        """Calculate and cache CRC32 checksum"""
        if self._crc32 is None:
            self._crc32 = zlib.crc32(self.data) & 0xFFFFFFFF
        return self._crc32

    @property
    def md5(self) -> str:
        """Calculate and cache MD5 hash"""
        if self._md5 is None:
            md5_hash = hashlib.md5()
            md5_hash.update(self.data)
            self._md5 = md5_hash.hexdigest()
        return self._md5


# ---------------------------------------------------------------------------
# MQTT client
# ---------------------------------------------------------------------------

class MQTTClient:
    """MQTT client wrapper with connection management"""

    def __init__(self, config: MQTTConfig):
        self.config = config
        self._setup_client()
        self._connected = False

    def _setup_client(self):
        """Set up MQTT client based on configuration"""
        if self.config.protocol == "mqtt":
            self.client = mqtt.Client(
                client_id=self.config.client_id,
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2
            )
        elif self.config.protocol == "ws":
            self.client = mqtt.Client(
                client_id=self.config.client_id,
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
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


# ---------------------------------------------------------------------------
# OTA updater
# ---------------------------------------------------------------------------

class OTAUpdater:
    """Main OTA firmware update orchestrator"""

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

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._send_start_message()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {reason_code}")
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


# ---------------------------------------------------------------------------
# File transfer (config files, certificates, or any arbitrary file)
# ---------------------------------------------------------------------------

class FileTransfer:
    """Transfers an arbitrary file to the device via MQTT.
    Uses 'name' + 'fileSize' + 'crc32' + 'md5' in the start message instead of 'binId',
    which signals to the device that this is a generic file transfer, not a firmware update."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, file_entry: FileEntry):
        self.device_config = device_config
        self.file_entry = file_entry
        self.file_provider = FileDataProvider(file_entry.local_path)
        self.mqtt_client = MQTTClient(mqtt_config)

        # Transfer state management (reuses OTAState for consistency)
        self.state = OTAState.IDLE
        self.piece_number = 0
        self.remaining_bytes = 0
        self.timer_start = 0
        self.piece_size = 100
        self.timeout_seconds = 25
        self.progress_bar: Optional[tqdm] = None

        # Set up MQTT callbacks
        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._send_start_message()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {reason_code}")
            self.state = OTAState.ERROR

    def _on_message(self, client, userdata, msg):
        """Handle incoming MQTT messages"""
        try:
            message = json.loads(msg.payload.decode())
            self._process_response(message)
        except json.JSONDecodeError as e:
            logging.error(f"Failed to parse MQTT message: {e}")
            self.state = OTAState.ERROR

    def _send_start_message(self):
        """Send file transfer start message to device.
        Uses 'name' (device destination path) instead of 'binId' to signal a generic file transfer."""
        start_message = {
            "name": self.file_entry.device_path,
            "fileSize": self.file_provider.size,
            "crc32": self.file_provider.crc32,
            "md5": self.file_provider.md5
        }

        self.mqtt_client.publish(self.device_config.send_topic, json.dumps(start_message))
        logging.info(f"File transfer started - File: {self.file_entry.local_path.name}")
        logging.info(f"  Device path: {self.file_entry.device_path}")
        logging.info(f"  Size:  {self.file_provider.size} bytes")
        logging.info(f"  CRC32: {self.file_provider.crc32}")
        logging.info(f"  MD5:   {self.file_provider.md5}")

        self.state = OTAState.WAIT_START_ACK
        self.remaining_bytes = self.file_provider.size
        self.timer_start = time.time()

    def _process_response(self, message: Dict[str, Any]):
        """Process response messages from device"""
        if "type" not in message:
            logging.warning("Received message without 'type' field")
            return

        ack = message["type"] != 0

        if self.state == OTAState.WAIT_START_ACK and ack:
            logging.info("Start acknowledgment received, beginning file transfer")
            self.progress_bar = tqdm(
                total=self.file_provider.size,
                desc=f"Sending {self.file_entry.local_path.name}",
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

    def _send_file_piece(self):
        """Send a piece of the file to the device"""
        offset = self.file_provider.size - self.remaining_bytes
        read_size = min(self.remaining_bytes, self.piece_size)
        data = self.file_provider.data[offset:offset + read_size]

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
        """Process current transfer state"""
        current_time = time.time()

        if self.state == OTAState.SENDING_FW:
            if self.remaining_bytes > 0:
                self._send_file_piece()
            else:
                # Close progress bar immediately when transfer is complete
                self._close_progress_bar()
                logging.info("All file pieces sent, waiting for final verification")
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
        """Run the file transfer process"""
        if not self.mqtt_client.connect():
            return False

        try:
            while self.state not in {OTAState.DONE, OTAState.ERROR}:
                self.mqtt_client.loop(timeout=0.1)
                self._process_state()

            success = self.state == OTAState.DONE
            if success:
                logging.info("File transfer completed successfully")
            else:
                logging.error("File transfer failed")

            return success

        except KeyboardInterrupt:
            logging.info("File transfer interrupted by user")
            return False
        except Exception as e:
            logging.error(f"Unexpected error during file transfer: {e}")
            return False
        finally:
            self._cleanup()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Sentinel used in the action menu to identify the firmware upload option
_FW_OPTION = "Firmware upload"


def select_target(projects: List[ProjectEntry]) -> Optional[Tuple[ProjectEntry, DeviceEntry, Optional[FileEntry]]]:
    """
    Interactive three-level menu:
      1. Select project
      2. Select device
      3. Select action (firmware upload or a configured file transfer)
    Returns (project, device, file_entry) where file_entry is None for firmware upload,
    or None if the user cancelled.
    """
    menu = MenuSelector()

    while True:
        # --- Level 1: project selection ---
        project_names = [p.name for p in projects]
        choice = menu.select("Select project", project_names, show_back=False)

        if choice in (MenuSelector.CANCEL, None):
            return None

        selected_project = next(p for p in projects if p.name == choice)

        while True:
            # --- Level 2: device selection ---
            device_labels = [d.display_name for d in selected_project.devices]
            choice = menu.select(
                f"Select device  [{selected_project.name}]",
                device_labels,
                show_back=True
            )

            if choice == MenuSelector.CANCEL:
                return None
            if choice == MenuSelector.BACK:
                break  # go back to project selection

            selected_device = next(d for d in selected_project.devices if d.display_name == choice)

            while True:
                # --- Level 3: action selection ---
                # Always offer firmware upload; append any device-specific files below
                action_options = [_FW_OPTION] + [f.name for f in selected_device.files]
                choice = menu.select(
                    f"Select action  [{selected_device.display_name}]",
                    action_options,
                    show_back=True
                )

                if choice == MenuSelector.CANCEL:
                    return None
                if choice == MenuSelector.BACK:
                    break  # go back to device selection

                if choice == _FW_OPTION:
                    return selected_project, selected_device, None
                else:
                    selected_file = next(f for f in selected_device.files if f.name == choice)
                    return selected_project, selected_device, selected_file


def main():
    """Main entry point"""
    try:
        config_manager = ConfigManager(__file__)
        device_manager = DeviceManager(__file__)

        print("Loading configuration...")
        mqtt_config = config_manager.load_mqtt_config()
        projects = device_manager.load()

        # Interactive target selection (project → device → action)
        result = select_target(projects)
        if result is None:
            print("Cancelled.")
            sys.exit(0)

        selected_project, selected_device, selected_file = result

        print(f"\n✅ Configuration loaded successfully:")
        print(f"  Protocol:    {mqtt_config.protocol}")
        print(f"  Host:        {mqtt_config.host}")
        print(f"  Port:        {mqtt_config.port}")
        print(f"  Client ID:   {mqtt_config.client_id}")
        print(f"  TLS Enabled: {mqtt_config.tls_enabled}")
        print(f"  Auth:        {'Yes' if mqtt_config.username else 'No'}")
        print(f"\n🎯 Target:")
        print(f"  Project:     {selected_project.name}  ({selected_project.pio_project})")
        print(f"  Device:      {selected_device.display_name}")

        device_config = DeviceConfig(
            mac_address=selected_device.mac,
            project_name=selected_project.pio_project
        )

        if selected_file is None:
            # Firmware upload
            firmware_path = config_manager.get_firmware_path(selected_project.pio_project)
            print(f"  Action:      Firmware feltöltés")
            print(f"  Firmware:    {firmware_path}")
            print()
            worker = OTAUpdater(device_config, mqtt_config, firmware_path)
        else:
            # Generic file transfer
            if not selected_file.local_path.exists():
                print(f"❌ File Error: Local file not found: {selected_file.local_path}")
                sys.exit(1)
            print(f"  Action:      {selected_file.name}")
            print(f"  Local file:  {selected_file.local_path}")
            print(f"  Device path: {selected_file.device_path}")
            print()
            worker = FileTransfer(device_config, mqtt_config, selected_file)

        # Set up signal handler for graceful shutdown
        def signal_handler(sig, frame):
            logging.info("Received interrupt signal, shutting down...")
            worker._cleanup()
            sys.exit(0)

        # Register signal handlers (SIGINT works on both Windows and Unix)
        signal.signal(signal.SIGINT, signal_handler)

        # On Unix systems, also handle SIGTERM
        if hasattr(signal, 'SIGTERM'):
            signal.signal(signal.SIGTERM, signal_handler)

        # Run the selected operation
        success = worker.run()
        sys.exit(0 if success else 1)

    except FileNotFoundError as e:
        print(f"❌ File Error: {e}")
        sys.exit(1)
    except ValueError as e:
        print(f"❌ Configuration Error: {e}")
        sys.exit(1)
    except yaml.YAMLError as e:
        print(f"❌ YAML Parsing Error: {e}")
        sys.exit(1)
    except Exception as e:
        logging.error(f"Unexpected error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()