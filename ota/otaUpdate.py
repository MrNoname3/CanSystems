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
import base64
import sys
import signal
import enum
import logging
import uuid
import hashlib
import curses
from pathlib import Path
from typing import Optional, Dict, Any, List, ClassVar
from dataclasses import dataclass, field
from tqdm import tqdm
import yaml
import paho.mqtt.client as mqtt


# ---------------------------------------------------------------------------
# Enums & Data classes
# ---------------------------------------------------------------------------

class TransferState(enum.Enum):
    """States for the OTA update / file transfer / command process"""
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
    host: str = ""                            # Server hostname or IP
    port: int = 0                             # Server port (auto-determined if 0)
    basepath: str = "/"                       # Only used with WebSocket
    client_id: str = ""                       # Unique client ID
    username: Optional[str] = None            # MQTT username
    password: Optional[str] = None            # MQTT password
    tls_enabled: bool = False                 # Use TLS encryption
    cafile: Optional[str] = None             # CA certificate file path

    # Default ports keyed by (protocol, tls_enabled)
    _DEFAULT_PORTS: ClassVar[dict] = {
        ("mqtt", False): 1883,
        ("mqtt", True):  8883,
        ("ws",   False): 80,
        ("ws",   True):  443,
    }

    def __post_init__(self):
        """Validate and set defaults after initialization"""
        if self.protocol not in ("mqtt", "ws"):
            raise ValueError(f"Unsupported protocol: {self.protocol}. Must be 'mqtt' or 'ws'")

        # Set default port based on protocol and TLS
        if not self.port:
            self.port = self._DEFAULT_PORTS[(self.protocol, self.tls_enabled)]

        if not (1 <= self.port <= 65535):
            raise ValueError(f"Invalid port: {self.port}. Must be between 1 and 65535")

        if not self.client_id:
            self.client_id = f"Python_OTA_{uuid.uuid4().hex[:8]}"

        # Validate CA file path if provided (handle Windows paths correctly)
        if self.cafile is not None:
            ca_path = Path(self.cafile).expanduser().resolve()
            if not ca_path.exists():
                raise FileNotFoundError(f"CA certificate file not found: {ca_path}")
            self.cafile = str(ca_path)

        if not self.host.strip():
            raise ValueError("Host is required and cannot be empty")


@dataclass
class CommandEntry:
    """A single command entry from devices.yaml"""
    name: str                               # Display name shown in the menu
    cmd: str                                # Command string sent to the device
    description: Optional[str] = None       # Optional description shown in the menu

    @property
    def display_name(self) -> str:
        """Return the formatted menu label, including description if present."""
        if self.description:
            return f"{self.name}  ({self.description})"
        return self.name


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
    commands: List[CommandEntry] = field(default_factory=list)  # Merged common + project commands.
    devices: List[DeviceEntry] = field(default_factory=list)


@dataclass
class ActionResult:
    """Holds the result of the interactive menu selection.
    Exactly one of `file` or `command` is not None; the other is always None.
    If both are None, the selected action is a firmware upload."""
    project: ProjectEntry
    device: DeviceEntry
    file: Optional[FileEntry] = None        # Set when a file transfer was selected.
    command: Optional[CommandEntry] = None  # Set when a command was selected.


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

        # Parse common commands shared across all projects.
        common_commands = self._parse_commands(
            data.get('common', {}).get('commands', []),
            context="common"
        )

        projects: List[ProjectEntry] = []
        for p in data['projects']:
            if 'name' not in p or 'pio_project' not in p:
                raise ValueError("Each project entry must have 'name' and 'pio_project' fields")

            # Merge common commands with project-level commands.
            merged_commands = common_commands + self._parse_commands(p.get('commands', []), context=p['name'])

            devices: List[DeviceEntry] = []
            for d in p.get('devices', []):
                if 'mac' not in d:
                    raise ValueError(f"Each device entry must have a 'mac' field (project: {p['name']})")

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
                commands=merged_commands,
                devices=devices
            ))

        if not projects:
            raise ValueError("devices.yaml contains no projects")

        return projects

    def _parse_commands(self, raw: list, context: str) -> List[CommandEntry]:
        """Parse a list of raw command dicts into CommandEntry objects."""
        commands = []
        for c in raw:
            if 'name' not in c or 'cmd' not in c:
                raise ValueError(
                    f"Each command entry must have 'name' and 'cmd' fields (context: {context})"
                )
            commands.append(CommandEntry(
                name=c['name'],
                cmd=c['cmd'],
                description=c.get('description')
            ))
        return commands


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
        return curses.wrapper(self._run, title, options, show_back)

    def _run(self, stdscr, title: str, options: List[str], show_back: bool):
        curses.curs_set(0)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)   # selected item
        curses.init_pair(2, curses.COLOR_CYAN,  -1)                  # title
        curses.init_pair(3, curses.COLOR_YELLOW, -1)                 # hint line

        # Build full item list: real options + navigation entries
        nav_items = (["← Back"] if show_back else []) + ["✕ Cancel"]
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
                if idx == current:
                    stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
                    stdscr.addstr(row, 2, f"   {item} ".ljust(width - 4)[:width - 4])
                    stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)
                else:
                    if is_nav:
                        stdscr.attron(curses.A_DIM)
                    stdscr.addstr(row, 2, f"   {item}"[:width - 4])
                    if is_nav:
                        stdscr.attroff(curses.A_DIM)

            # Hint
            stdscr.attron(curses.color_pair(3))
            stdscr.addstr(height - 1, 2, "↑↓ Navigate   Enter Select   Esc Cancel"[:width - 4])
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
                config_data = yaml.safe_load(file) or {}
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse YAML configuration file: {e}")
        except UnicodeDecodeError as e:
            raise ValueError(f"Configuration file encoding error: {e}")
        except Exception as e:
            raise IOError(f"Failed to read configuration file: {e}")

        try:
            return MQTTConfig(
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
        return len(self.firmware_data)

    @property
    def md5(self) -> str:
        """Calculate and cache MD5 hash"""
        if self._md5 is None:
            self._md5 = hashlib.md5(self.firmware_data).hexdigest()
        return self._md5

    @property
    def firmware_id(self) -> str:
        """Extract and cache firmware ID"""
        if self._firmware_id is None:
            fw_id = self._extract_firmware_id()
            if fw_id is None:
                raise ValueError("Could not extract firmware ID from binary")
            self._firmware_id = fw_id
        return self._firmware_id

    def _read_firmware(self) -> bytes:
        """Read firmware binary file"""
        try:
            return self.firmware_path.read_bytes()
        except IOError as e:
            raise IOError(f"Failed to read firmware file: {e}")

    def _extract_firmware_id(self) -> Optional[str]:
        """Extract firmware identifier from binary"""
        begin_of_id = b"project_"
        start_index = self.firmware_data.find(begin_of_id)

        if start_index != -1:
            end_index = self.firmware_data.find(b'\0', start_index + len(begin_of_id))
            if end_index != -1:
                identifier = self.firmware_data[start_index:end_index].decode('utf-8')
                logging.info(f"Firmware ID found: \"{identifier}\"")
                return identifier

        logging.error("Firmware ID not found in binary")
        return None


# ---------------------------------------------------------------------------
# Generic file data provider (used by FileTransfer)
# ---------------------------------------------------------------------------

class FileDataProvider:
    """Reads an arbitrary binary file and provides size and checksum properties.
    For .json files, automatically serializes the content (removes whitespace)
    before transfer, unless the file is already serialized. This ensures the
    device always receives compact JSON regardless of how it is stored locally.
    The size and MD5 are both computed from the serialized bytes."""

    def __init__(self, file_path: Path):
        self.file_path = file_path
        self._data: Optional[bytes] = None
        self._md5: Optional[str] = None

    @property
    def data(self) -> bytes:
        """Lazy loading of file data. JSON files are serialized automatically."""
        if self._data is None:
            try:
                raw = self.file_path.read_bytes()
            except IOError as e:
                raise IOError(f"Failed to read file: {e}")
            self._data = self._serialize_json(raw) if self.file_path.suffix.lower() == '.json' else raw
        return self._data

    def _serialize_json(self, raw: bytes) -> bytes:
        """Parse and re-serialize JSON to remove whitespace.
        If the content is already serialized (compact), it is returned as-is.
        Raises ValueError if the content is not valid JSON."""
        try:
            parsed = json.loads(raw.decode('utf-8'))
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            raise ValueError(f"Invalid JSON file '{self.file_path.name}': {e}")

        serialized_bytes = json.dumps(parsed, separators=(',', ':'), ensure_ascii=False).encode('utf-8')

        if serialized_bytes == raw.strip():
            logging.info(f"JSON file '{self.file_path.name}' is already serialized, no transformation needed")
        else:
            logging.info(f"JSON file '{self.file_path.name}' serialized: {len(raw)} -> {len(serialized_bytes)} bytes")

        return serialized_bytes

    @property
    def size(self) -> int:
        """Get file size (of serialized content for JSON files)"""
        return len(self.data)

    @property
    def md5(self) -> str:
        """Calculate and cache MD5 hash (of serialized content for JSON files)"""
        if self._md5 is None:
            self._md5 = hashlib.md5(self.data).hexdigest()
        return self._md5


# ---------------------------------------------------------------------------
# MQTT client
# ---------------------------------------------------------------------------

class MQTTClient:
    """MQTT client wrapper with connection management"""

    def __init__(self, config: MQTTConfig):
        self.config = config
        self._setup_client()

    def _setup_client(self):
        """Set up MQTT client based on configuration"""
        transport = "websockets" if self.config.protocol == "ws" else "tcp"
        self.client = mqtt.Client(
            client_id=self.config.client_id,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            transport=transport
        )

        if self.config.protocol == "ws" and hasattr(self.client, 'ws_set_options'):
            self.client.ws_set_options(path=self.config.basepath)

        if self.config.username is not None and self.config.password is not None:
            self.client.username_pw_set(username=self.config.username, password=self.config.password)

        if self.config.tls_enabled:
            self.client.tls_set(ca_certs=self.config.cafile)  # cafile=None uses system store

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
# Base transfer class (shared logic for OTAUpdater and FileTransfer)
# ---------------------------------------------------------------------------

class _BaseTransfer:
    """Common state-machine and MQTT plumbing shared by OTAUpdater and FileTransfer.

    Subclasses must implement:
      - `_build_start_message()  -> dict`   – the initial JSON payload
      - `_start_log_info()`                 – log lines shown after connect
      - `_progress_desc`         (property) – tqdm description string
      - `data`                   (property) – bytes to send
      - `size`                   (property) – total byte count
    """

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig):
        self.device_config = device_config
        self.mqtt_client = MQTTClient(mqtt_config)

        self.state = TransferState.IDLE
        self.piece_number = 0
        self.remaining_bytes = 0
        self.timer_start = 0.0
        self.piece_size = 100
        self.timeout_seconds = 25
        self.progress_bar: Optional[tqdm] = None

        # Queue for incoming MQTT messages to avoid race conditions between
        # the MQTT callback thread and the main loop
        self._pending_messages: List[Dict[str, Any]] = []

        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)
        logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

    # --- Abstract interface ---------------------------------------------------

    def _build_start_message(self) -> dict:
        raise NotImplementedError

    def _start_log_info(self):
        raise NotImplementedError

    @property
    def _progress_desc(self) -> str:
        raise NotImplementedError

    @property
    def data(self) -> bytes:
        raise NotImplementedError

    @property
    def size(self) -> int:
        raise NotImplementedError

    # --- MQTT callbacks -------------------------------------------------------

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._send_start_message()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {reason_code}")
            self.state = TransferState.ERROR

    def _on_message(self, client, userdata, msg):
        """Queue incoming MQTT messages for processing in the main loop.
        This avoids race conditions where an ACK arrives before the state
        machine has transitioned to the expected state."""
        try:
            self._pending_messages.append(json.loads(msg.payload.decode()))
        except json.JSONDecodeError as e:
            logging.error(f"Failed to parse MQTT message: {e}")
            self.state = TransferState.ERROR

    # --- Internal helpers -----------------------------------------------------

    def _send_start_message(self):
        """Publish the start message and transition to WAIT_START_ACK."""
        self._start_log_info()
        self.mqtt_client.publish(self.device_config.send_topic, json.dumps(self._build_start_message()))
        self.state = TransferState.WAIT_START_ACK
        self.remaining_bytes = self.size
        self.timer_start = time.time()

    def _process_response(self, message: Dict[str, Any]):
        """Process ACK/NACK response messages from device."""
        if "type" not in message:
            logging.warning("Received message without 'type' field")
            return

        ack = message["type"] != 0

        if self.state == TransferState.WAIT_START_ACK and ack:
            logging.info("Start acknowledgment received, beginning transfer")
            self.progress_bar = tqdm(total=self.size, desc=self._progress_desc, unit="B", unit_scale=True)
            self.state = TransferState.SENDING_FW

        elif self.state == TransferState.WAIT_PIECE_ACK and ack:
            if self.remaining_bytes > 0:
                self.state = TransferState.SENDING_FW
            else:
                self._finish_sending()

        elif self.state == TransferState.WAIT_CHECK_ACK and ack:
            self.state = TransferState.DONE

        else:
            logging.error(f"Received negative acknowledgment or unexpected state. Message: {message}")
            self.state = TransferState.ERROR

    def _send_piece(self):
        """Send the next data piece to the device."""
        offset = self.size - self.remaining_bytes
        read_size = min(self.remaining_bytes, self.piece_size)

        self.mqtt_client.publish(self.device_config.send_topic, json.dumps({
            "piece": self.piece_number,
            "data": base64.b64encode(self.data[offset:offset + read_size]).decode('utf-8')
        }))

        self.state = TransferState.WAIT_PIECE_ACK
        self.timer_start = time.time()
        self.piece_number += 1
        self.remaining_bytes -= read_size

        if self.progress_bar:
            self.progress_bar.update(read_size)

    def _close_progress_bar(self):
        """Close and clean up the progress bar."""
        if self.progress_bar:
            self.progress_bar.close()
            self.progress_bar = None

    def _finish_sending(self):
        """Transition to WAIT_CHECK_ACK after all pieces have been sent."""
        self._close_progress_bar()
        logging.info("All pieces sent, waiting for final verification")
        self.state = TransferState.WAIT_CHECK_ACK
        self.timer_start = time.time()

    def _process_state(self):
        """Process current transfer state.
        Pending messages are handled first to ensure the state machine has
        fully transitioned before acting on incoming ACKs."""
        for message in self._pending_messages:
            self._process_response(message)
        self._pending_messages.clear()

        if self.state == TransferState.SENDING_FW:
            if self.remaining_bytes > 0:
                self._send_piece()
            else:
                self._finish_sending()

        elif self.state in {TransferState.WAIT_START_ACK, TransferState.WAIT_PIECE_ACK, TransferState.WAIT_CHECK_ACK}:
            if time.time() - self.timer_start > self.timeout_seconds:
                logging.error(f"Timeout occurred in state: {self.state.name}")
                self.state = TransferState.ERROR

    def _cleanup(self):
        """Clean up resources."""
        self._close_progress_bar()
        self.mqtt_client.disconnect()

    def run(self) -> bool:
        """Run the transfer process."""
        if not self.mqtt_client.connect():
            return False

        try:
            while self.state not in {TransferState.DONE, TransferState.ERROR}:
                self.mqtt_client.loop(timeout=0.1)
                self._process_state()

            success = self.state == TransferState.DONE
            logging.info("Transfer completed successfully" if success else "Transfer failed")
            return success

        except KeyboardInterrupt:
            logging.info("Transfer interrupted by user")
            return False
        except Exception as e:
            logging.error(f"Unexpected error during transfer: {e}")
            return False
        finally:
            self._cleanup()


# ---------------------------------------------------------------------------
# OTA updater
# ---------------------------------------------------------------------------

class OTAUpdater(_BaseTransfer):
    """Firmware OTA update – sends firmware.bin to the device via MQTT."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, firmware_path: Path):
        super().__init__(device_config, mqtt_config)
        self.firmware_manager = FirmwareManager(firmware_path)

    @property
    def data(self) -> bytes:
        return self.firmware_manager.firmware_data

    @property
    def size(self) -> int:
        return self.firmware_manager.size

    @property
    def _progress_desc(self) -> str:
        return "Sending Firmware"

    def _build_start_message(self) -> dict:
        return {
            "fileSize": self.firmware_manager.size,
            "md5":      self.firmware_manager.md5,
            "binId":    self.firmware_manager.firmware_id,
        }

    def _start_log_info(self):
        logging.info(f"OTA started - Size: {self.firmware_manager.size} bytes")
        logging.info(f"  MD5:   {self.firmware_manager.md5}")


# ---------------------------------------------------------------------------
# File transfer (config files, certificates, or any arbitrary file)
# ---------------------------------------------------------------------------

class FileTransfer(_BaseTransfer):
    """Transfers an arbitrary file to the device via MQTT.
    Uses 'name' + 'fileSize' + 'md5' in the start message instead of 'binId',
    which signals to the device that this is a generic file transfer, not a firmware update."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, file_entry: FileEntry):
        super().__init__(device_config, mqtt_config)
        self.file_entry = file_entry
        self.file_provider = FileDataProvider(file_entry.local_path)

    @property
    def data(self) -> bytes:
        return self.file_provider.data

    @property
    def size(self) -> int:
        return self.file_provider.size

    @property
    def _progress_desc(self) -> str:
        return f"Sending {self.file_entry.local_path.name}"

    def _build_start_message(self) -> dict:
        return {
            "name":     self.file_entry.device_path,
            "fileSize": self.file_provider.size,
            "md5":      self.file_provider.md5,
        }

    def _start_log_info(self):
        logging.info(f"File transfer started - File: {self.file_entry.local_path.name}")
        logging.info(f"  Device path: {self.file_entry.device_path}")
        logging.info(f"  Size:  {self.file_provider.size} bytes")
        logging.info(f"  MD5:   {self.file_provider.md5}")


# ---------------------------------------------------------------------------
# Command sender
# ---------------------------------------------------------------------------

class CommandSender(_BaseTransfer):
    """Sends a single command to the device via MQTT and waits for an ACK response.
    The command is sent as a JSON payload with a 'cmd' key. A timeout is applied
    while waiting for the device acknowledgment, consistent with the other workers."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, command: CommandEntry):
        super().__init__(device_config, mqtt_config)
        # Reuse TransferState: IDLE → WAIT_START_ACK → DONE / ERROR
        self.command = command

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._send_command()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {reason_code}")
            self.state = TransferState.ERROR

    def _send_command(self):
        """Publish the command message to the device topic."""
        self.mqtt_client.publish(self.device_config.send_topic, json.dumps({"cmd": self.command.cmd}))
        logging.info(f"Command sent: '{self.command.cmd}'")
        self.state = TransferState.WAIT_START_ACK
        self.timer_start = time.time()

    def _process_state(self):
        """Process pending messages and check for timeout."""
        for message in self._pending_messages:
            if "type" not in message:
                logging.warning("Received message without 'type' field")
                continue
            if message["type"] != 0:
                self.state = TransferState.DONE
            else:
                logging.error(f"Command rejected by device. Message: {message}")
                self.state = TransferState.ERROR
        self._pending_messages.clear()

        if self.state == TransferState.WAIT_START_ACK:
            if time.time() - self.timer_start > self.timeout_seconds:
                logging.error("Timeout waiting for command acknowledgment")
                self.state = TransferState.ERROR

    def run(self) -> bool:
        """Send the command and wait for the device acknowledgment."""
        if not self.mqtt_client.connect():
            return False

        try:
            while self.state not in {TransferState.DONE, TransferState.ERROR}:
                self.mqtt_client.loop(timeout=0.1)
                self._process_state()

            success = self.state == TransferState.DONE
            if success:
                logging.info(f"Command '{self.command.cmd}' acknowledged successfully")
            else:
                logging.error(f"Command '{self.command.cmd}' failed or timed out")
            return success

        except KeyboardInterrupt:
            logging.info("Command interrupted by user")
            return False
        except Exception as e:
            logging.error(f"Unexpected error during command sending: {e}")
            return False
        finally:
            self._cleanup()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Sentinel used in the action menu to identify the firmware upload option.
_FW_OPTION = "Firmware upload"


def select_target(projects: List[ProjectEntry]) -> Optional[ActionResult]:
    """
    Interactive three-level menu:
      1. Select project
      2. Select device
      3. Select action (firmware upload, file transfer, or command)
    Returns an ActionResult, or None if the user cancelled.
    """
    menu = MenuSelector()

    while True:
        # --- Level 1: project selection ---
        project_map = {p.name: p for p in projects}
        choice = menu.select("Select project", list(project_map), show_back=False)
        if choice in (MenuSelector.CANCEL, None):
            return None

        selected_project = project_map[choice]

        while True:
            # --- Level 2: device selection ---
            device_map = {d.display_name: d for d in selected_project.devices}
            choice = menu.select(f"Select device  [{selected_project.name}]", list(device_map), show_back=True)

            if choice == MenuSelector.CANCEL:
                return None
            if choice == MenuSelector.BACK:
                break  # go back to project selection

            selected_device = device_map[choice]

            while True:
                # --- Level 3: action selection ---
                # Order: firmware upload → file transfers → commands.
                file_map    = {f.name: f for f in selected_device.files}
                command_map = {c.display_name: c for c in selected_project.commands}
                action_options = [_FW_OPTION] + list(file_map) + list(command_map)

                choice = menu.select(f"Select action  [{selected_device.display_name}]", action_options, show_back=True)

                if choice == MenuSelector.CANCEL:
                    return None
                if choice == MenuSelector.BACK:
                    break  # go back to device selection

                if choice == _FW_OPTION:
                    return ActionResult(project=selected_project, device=selected_device)
                if choice in file_map:
                    return ActionResult(project=selected_project, device=selected_device, file=file_map[choice])
                if choice in command_map:
                    return ActionResult(project=selected_project, device=selected_device, command=command_map[choice])


def _build_worker(result: ActionResult, config_manager: ConfigManager, mqtt_config: MQTTConfig):
    """Factory: create the appropriate worker (OTAUpdater / FileTransfer / CommandSender)."""
    device_config = DeviceConfig(mac_address=result.device.mac, project_name=result.project.pio_project)

    if result.command is not None:
        print(f"  Action:      {result.command.display_name}")
        print()
        return CommandSender(device_config, mqtt_config, result.command)

    if result.file is not None:
        if not result.file.local_path.exists():
            raise FileNotFoundError(f"Local file not found: {result.file.local_path}")
        print(f"  Action:      {result.file.name}")
        print(f"  Local file:  {result.file.local_path}")
        print(f"  Device path: {result.file.device_path}")
        print()
        return FileTransfer(device_config, mqtt_config, result.file)

    # Firmware upload
    firmware_path = config_manager.get_firmware_path(result.project.pio_project)
    print(f"  Action:      Firmware upload")
    print(f"  Firmware:    {firmware_path}")
    print()
    return OTAUpdater(device_config, mqtt_config, firmware_path)


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

        print(f"\n✅ Configuration loaded successfully:")
        print(f"  Protocol:    {mqtt_config.protocol}")
        print(f"  Host:        {mqtt_config.host}")
        print(f"  Port:        {mqtt_config.port}")
        print(f"  Client ID:   {mqtt_config.client_id}")
        print(f"  TLS Enabled: {mqtt_config.tls_enabled}")
        print(f"  Auth:        {'Yes' if mqtt_config.username else 'No'}")
        print(f"\n🎯 Target:")
        print(f"  Project:     {result.project.name}  ({result.project.pio_project})")
        print(f"  Device:      {result.device.display_name}")

        worker = _build_worker(result, config_manager, mqtt_config)

        # Set up signal handler for graceful shutdown
        def signal_handler(sig, frame):
            logging.info("Received interrupt signal, shutting down...")
            worker._cleanup()
            sys.exit(0)

        signal.signal(signal.SIGINT, signal_handler)
        if hasattr(signal, 'SIGTERM'):
            signal.signal(signal.SIGTERM, signal_handler)

        sys.exit(0 if worker.run() else 1)

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