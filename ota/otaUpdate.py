#!/usr/bin/env python3
"""
ESP8266/ESP32 Over-The-Air (OTA) Update Tool via MQTT

This tool provides a modular approach to updating ESP devices firmware
and transferring configuration files through MQTT communication with
proper error handling and progress tracking.
Uses a YAML secrets file (secrets.yaml, git-ignored) and device list (devices.yaml).
"""

import json
import os
import re
import shutil
import subprocess
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
from types import FrameType
from typing import Optional, Dict, Any, List, ClassVar, Callable, cast
from dataclasses import dataclass, field
from tqdm import tqdm
import yaml
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion


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
    _DEFAULT_PORTS: ClassVar[dict[tuple[str, bool], int]] = {
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
    """A transferable file entry from devices.yaml.
    Exactly one of `local_path` (a file on disk), `render` (content generated
    at send time; currently only "server_json") or `content` (an inline mapping
    sent as compact JSON) is set."""
    name: str                        # Display name shown in the menu
    device_path: str                 # Destination path on the device (sent as 'name' in JSON)
    local_path: Optional[Path] = None  # Local path to the file (relative to ota/ directory)
    render: Optional[str] = None       # Renderer id for generated content ("server_json")
    content: Optional[Dict[str, Any]] = None  # Inline JSON content from devices.yaml


@dataclass
class DeviceEntry:
    """A single device entry from devices.yaml"""
    mac: str
    friendly_name: Optional[str] = None
    files: List[FileEntry] = field(default_factory=list[FileEntry])
    server_config: Dict[str, Any] = field(default_factory=dict[str, Any])  # Non-secret server.json fields (e.g. haDiscovery).

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
    commands: List[CommandEntry] = field(default_factory=list[CommandEntry])  # Merged common + project commands.
    devices: List[DeviceEntry] = field(default_factory=list[DeviceEntry])


@dataclass
class ActionResult:
    """Holds the result of the interactive menu selection.
    At most one of `file` / `command` is not None, or `provision` is True.
    If none of them is set, the selected action is a firmware upload."""
    project: ProjectEntry
    device: DeviceEntry
    file: Optional[FileEntry] = None        # Set when a file transfer was selected.
    command: Optional[CommandEntry] = None  # Set when a command was selected.
    provision: bool = False                 # Set when USB provisioning was selected.


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

        projects = [self._parse_project(p, common_commands) for p in data['projects']]

        if not projects:
            raise ValueError("devices.yaml contains no projects")

        return projects

    def _parse_commands(self, raw: list[Any], context: str) -> List[CommandEntry]:
        """Parse a list of raw command dicts into CommandEntry objects."""
        commands: list[CommandEntry] = []
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

    def _parse_file(self, f: dict[str, Any], mac: str) -> FileEntry:
        """Parse a single file entry dict into a FileEntry object."""
        if 'name' not in f or 'device_path' not in f:
            raise ValueError(
                f"Each file entry must have 'name' and 'device_path' fields (device: {mac})"
            )
        sources = [key for key in ('local_path', 'render', 'content') if key in f]
        if len(sources) != 1:
            raise ValueError(
                f"File entry '{f['name']}' must have exactly one of 'local_path', 'render' "
                f"or 'content' (device: {mac})"
            )
        if 'render' in f and f['render'] != 'server_json':
            raise ValueError(
                f"Unknown render type '{f['render']}' in file entry '{f['name']}' "
                f"(device: {mac}); only 'server_json' is supported"
            )
        if 'content' in f and not isinstance(f['content'], dict):
            raise ValueError(
                f"'content' must be a mapping in file entry '{f['name']}' (device: {mac})"
            )
        return FileEntry(
            name=f['name'],
            device_path=f['device_path'],
            local_path=self.script_dir / f['local_path'] if 'local_path' in f else None,
            render=f.get('render'),
            content=cast(Optional[Dict[str, Any]], f.get('content'))
        )

    def _parse_device(self, d: dict[str, Any], project_name: str) -> DeviceEntry:
        """Parse a single device entry dict into a DeviceEntry object."""
        if 'mac' not in d:
            raise ValueError(f"Each device entry must have a 'mac' field (project: {project_name})")
        server_config: Any = d.get('server_config', {})
        if not isinstance(server_config, dict):
            raise ValueError(f"'server_config' must be a mapping (device: {d['mac']})")
        return DeviceEntry(
            mac=d['mac'],
            friendly_name=d.get('friendly_name'),
            files=[self._parse_file(f, d['mac']) for f in d.get('files', [])],
            server_config=cast(Dict[str, Any], server_config)
        )

    def _parse_project(self, p: dict[str, Any], common_commands: List[CommandEntry]) -> ProjectEntry:
        """Parse a single project entry dict into a ProjectEntry object."""
        if 'name' not in p or 'pio_project' not in p:
            raise ValueError("Each project entry must have 'name' and 'pio_project' fields")
        # Merge common commands with project-level commands.
        merged_commands = common_commands + self._parse_commands(p.get('commands', []), context=p['name'])
        return ProjectEntry(
            name=p['name'],
            pio_project=p['pio_project'],
            commands=merged_commands,
            devices=[self._parse_device(d, p['name']) for d in p.get('devices', [])]
        )


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

    def _run(self, stdscr: "curses.window", title: str, options: List[str], show_back: bool) -> str:
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
    """Loads ota/secrets.yaml: the tool's broker connection, the per-device
    server.json secrets, and the optional pio executable override used by
    USB provisioning. secrets.yaml is git-ignored — it is the single file
    carried over manually when the repo is cloned on another machine."""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.parent_dir = self.script_dir.parent
        self.secrets_file = self.script_dir / 'secrets.yaml'
        self._secrets: Optional[dict[str, Any]] = None

    def _load_secrets(self) -> dict[str, Any]:
        """Load and cache secrets.yaml."""
        if self._secrets is not None:
            return self._secrets

        if not self.secrets_file.exists():
            raise FileNotFoundError(
                f"Secrets file not found: {self.secrets_file}\n"
                f"Create ota/secrets.yaml from the template in ota/README.md (it is git-ignored)."
            )

        try:
            with open(self.secrets_file, 'r', encoding='utf-8') as file:
                data: Any = yaml.safe_load(file) or {}
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse YAML secrets file: {e}")
        except UnicodeDecodeError as e:
            raise ValueError(f"Secrets file encoding error: {e}")
        except Exception as e:
            raise IOError(f"Failed to read secrets file: {e}")

        if not isinstance(data, dict):
            raise ValueError("secrets.yaml must be a YAML mapping")

        self._secrets = cast(dict[str, Any], data)
        return self._secrets

    def load_mqtt_config(self) -> MQTTConfig:
        """Build the tool's broker connection from the 'broker' section of secrets.yaml."""
        broker: Any = self._load_secrets().get('broker')
        if not isinstance(broker, dict):
            raise ValueError("secrets.yaml must contain a 'broker' mapping")
        broker_data = cast(dict[str, Any], broker)

        # A relative cafile is resolved against ota/, so the tool works from any CWD.
        cafile: Optional[str] = broker_data.get('cafile')
        if cafile is not None and not Path(cafile).expanduser().is_absolute():
            cafile = str(self.script_dir / cafile)

        try:
            return MQTTConfig(
                protocol=broker_data.get('protocol', 'mqtt'),
                host=broker_data.get('host', ''),
                port=broker_data.get('port', 0),
                basepath=broker_data.get('basepath', '/'),
                client_id=broker_data.get('client_id', "OtaUpdater"),
                username=broker_data.get('username'),
                password=broker_data.get('password'),
                tls_enabled=broker_data.get('tls_enabled', False),
                cafile=cafile
            )
        except (ValueError, FileNotFoundError) as e:
            raise ValueError(f"Configuration validation error: {e}")

    def device_server_secrets(self, mac: str) -> dict[str, Any]:
        """Secret server.json fields for one device: 'server_defaults' merged
        with (and overridden by) the device's entry under 'devices'."""
        data = self._load_secrets()

        defaults: Any = data.get('server_defaults') or {}
        if not isinstance(defaults, dict):
            raise ValueError("'server_defaults' in secrets.yaml must be a mapping")

        devices: Any = data.get('devices') or {}
        if not isinstance(devices, dict):
            raise ValueError("'devices' in secrets.yaml must be a mapping")

        entry: Any = cast(dict[Any, Any], devices).get(mac)
        if entry is None:
            raise ValueError(f"No entry for device {mac} under 'devices' in secrets.yaml")
        if not isinstance(entry, dict):
            raise ValueError(f"Device entry {mac} in secrets.yaml must be a mapping")

        return {**cast(dict[str, Any], defaults), **cast(dict[str, Any], entry)}

    def pio_command(self) -> str:
        """The pio executable used for provisioning: the optional top-level 'pio'
        key of secrets.yaml, else the standard PlatformIO penv location when it
        exists, else 'pio' from PATH."""
        override: Any = self._load_secrets().get('pio')
        if override:
            return str(Path(str(override)).expanduser())
        bundled = Path.home() / '.platformio' / 'penv' / 'bin' / 'pio'
        return str(bundled) if bundled.exists() else 'pio'

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

    @staticmethod
    def _strip_comments(text: str) -> str:
        """Remove // line comments and /* */ block comments from JSON-like text.
        String literals are left untouched."""
        def replacer(match: re.Match[str]) -> str:
            literal = cast("str | None", match.group(1))   # group 1 is the string-literal alternative
            return literal or ''
        return re.sub(r'("(?:[^"\\]|\\.)*")|//[^\r\n]*|/\*.*?\*/', replacer, text, flags=re.DOTALL)

    def _serialize_json(self, raw: bytes) -> bytes:
        """Strip comments, parse and re-serialize JSON to remove whitespace.
        If the content is already serialized (compact), it is returned as-is.
        Raises ValueError if the content is not valid JSON."""
        try:
            parsed = json.loads(self._strip_comments(raw.decode('utf-8')))
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
# server.json renderer
# ---------------------------------------------------------------------------

# Every field server.json may carry, in the order the rendered content emits them.
_SERVER_JSON_FIELDS = ("mqttUserName", "mqttPassword", "mqttServerUrl", "mqttServerPort",
                       "haDiscovery", "ssid", "password")
_SERVER_JSON_REQUIRED = ("mqttUserName", "mqttPassword", "mqttServerUrl", "mqttServerPort")


def render_server_json(secret_fields: Dict[str, Any], server_config: Dict[str, Any]) -> bytes:
    """Build the compact server.json content for one device.

    `secret_fields` comes from secrets.yaml (server_defaults merged with the
    per-device entry), `server_config` from the device's entry in devices.yaml
    (the non-secret fields, e.g. haDiscovery). devices.yaml wins on conflicts."""
    merged = {**secret_fields, **server_config}

    unknown = [k for k in merged if k not in _SERVER_JSON_FIELDS]
    if unknown:
        raise ValueError(f"Unknown server.json field(s): {', '.join(sorted(unknown))}")

    missing = [k for k in _SERVER_JSON_REQUIRED if k not in merged]
    if missing:
        raise ValueError(f"Missing required server.json field(s): {', '.join(missing)}")

    ordered = {k: merged[k] for k in _SERVER_JSON_FIELDS if k in merged}
    return json.dumps(ordered, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


class RenderedDataProvider:
    """Provides in-memory rendered content with the same interface as
    FileDataProvider (data / size / md5)."""

    def __init__(self, data: bytes):
        self._data = data
        self._md5: Optional[str] = None

    @property
    def data(self) -> bytes:
        return self._data

    @property
    def size(self) -> int:
        return len(self._data)

    @property
    def md5(self) -> str:
        if self._md5 is None:
            self._md5 = hashlib.md5(self._data).hexdigest()
        return self._md5


def build_file_provider(file_entry: FileEntry, device: DeviceEntry,
                        config_manager: "ConfigManager") -> "FileDataProvider | RenderedDataProvider":
    """Data provider for a file entry: disk-backed for local_path entries,
    rendered in memory for render and inline-content entries."""
    if file_entry.render == 'server_json':
        return RenderedDataProvider(render_server_json(
            config_manager.device_server_secrets(device.mac), device.server_config))
    if file_entry.content is not None:
        return RenderedDataProvider(
            json.dumps(file_entry.content, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))
    if file_entry.local_path is None:
        raise ValueError(f"File entry '{file_entry.name}' has no content source")
    if not file_entry.local_path.exists():
        raise FileNotFoundError(f"Local file not found: {file_entry.local_path}")
    return FileDataProvider(file_entry.local_path)


# ---------------------------------------------------------------------------
# USB provisioning (initial LittleFS image)
# ---------------------------------------------------------------------------

class Provisioner:
    """Builds and uploads the initial LittleFS image for a device over USB.

    Materializes every /config/* file entry of the device into <repo>/data
    (the PlatformIO filesystem source directory, git-ignored), runs
    `pio run -e <env> -t uploadfs`, then clears data/ again. This puts the
    device's own credentials on it from the very first flash — no shared
    bootstrap config is involved."""

    CONFIG_PREFIX = '/config/'

    def __init__(self, repo_root: Path, pio_cmd: str):
        self.repo_root = repo_root
        self.data_dir = repo_root / 'data'
        self.pio_cmd = pio_cmd

    def provision(self, project: ProjectEntry, device: DeviceEntry,
                  config_manager: "ConfigManager") -> bool:
        """Materialize the device's /config/* files into data/ and flash the
        filesystem image over serial. data/ is cleared before (so nothing
        stale ends up on the device) and after (so no credentials linger)."""
        entries = [f for f in device.files if f.device_path.startswith(self.CONFIG_PREFIX)]
        if not entries:
            raise ValueError(f"Device {device.mac} has no {self.CONFIG_PREFIX}* file entries to provision")

        self._clear_data_dir()
        try:
            for entry in entries:
                provider = build_file_provider(entry, device, config_manager)
                target = self.data_dir / entry.device_path.lstrip('/')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(provider.data)
                logging.info(f"Materialized {entry.device_path} ({provider.size} bytes)")
            return self._run_uploadfs(project.pio_project)
        finally:
            self._clear_data_dir()

    def _clear_data_dir(self):
        """Reset data/ to an empty directory so only the freshly materialized
        files end up in the filesystem image."""
        if self.data_dir.exists():
            shutil.rmtree(self.data_dir)
        self.data_dir.mkdir()

    def _run_uploadfs(self, pio_env: str) -> bool:
        """Run `pio run -e <env> -t uploadfs` from the repo root, streaming its output."""
        command = [self.pio_cmd, 'run', '-e', pio_env, '-t', 'uploadfs']
        env = dict(os.environ)
        env['VIRTUAL_ENV'] = ''    # a project .venv confuses pio's own virtualenv detection
        logging.info(f"Running: {' '.join(command)}")
        try:
            result = subprocess.run(command, cwd=self.repo_root, env=env)
        except FileNotFoundError:
            logging.error(f"pio executable not found: {self.pio_cmd} (set the 'pio' key in secrets.yaml)")
            return False
        return result.returncode == 0


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
            callback_api_version=CallbackAPIVersion.VERSION2,
            transport=transport
        )

        if self.config.protocol == "ws" and hasattr(self.client, 'ws_set_options'):
            self.client.ws_set_options(path=self.config.basepath)

        if self.config.username is not None and self.config.password is not None:
            self.client.username_pw_set(username=self.config.username, password=self.config.password)

        if self.config.tls_enabled:
            # paho's own tls_set stub leaves some parameters unannotated (partially unknown).
            self.client.tls_set(ca_certs=self.config.cafile)  # pyright: ignore[reportUnknownMemberType]  # cafile=None uses system store

    def set_callbacks(self, on_connect_callback: Callable[..., None], on_message_callback: Callable[..., None]) -> None:
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
        self.progress_bar: Optional["tqdm[Any]"] = None

        # Queue for incoming MQTT messages to avoid race conditions between
        # the MQTT callback thread and the main loop
        self._pending_messages: List[Dict[str, Any]] = []

        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)
        logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

    # --- Abstract interface ---------------------------------------------------

    def _build_start_message(self) -> dict[str, Any]:
        raise NotImplementedError

    def _start_log_info(self) -> None:
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

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        if reason_code == 0:
            logging.info("Successfully connected to MQTT broker")
            client.subscribe(self.device_config.receive_topic)
            self._on_connected()
        else:
            logging.error(f"Failed to connect to MQTT broker. Result code: {reason_code}")
            self.state = TransferState.ERROR

    def _on_connected(self):
        """Called after a successful connection. Override to customize post-connect behavior."""
        self._send_start_message()

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
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
            logging.error(f"Received NACK in state {self.state}, error code: {message.get('err', 0)}")
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

    def cleanup(self) -> None:
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
            self.cleanup()


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

    def _build_start_message(self) -> dict[str, Any]:
        return {
            "name":     "espFirmware",
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

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, file_entry: FileEntry,
                 provider: "FileDataProvider | RenderedDataProvider | None" = None):
        super().__init__(device_config, mqtt_config)
        self.file_entry = file_entry
        if provider is None:
            if file_entry.local_path is None:
                raise ValueError(f"File entry '{file_entry.name}' needs an explicit provider (rendered content)")
            provider = FileDataProvider(file_entry.local_path)
        self.file_provider = provider

    @property
    def data(self) -> bytes:
        return self.file_provider.data

    @property
    def size(self) -> int:
        return self.file_provider.size

    @property
    def _progress_desc(self) -> str:
        return f"Sending {Path(self.file_entry.device_path).name}"

    def _build_start_message(self) -> dict[str, Any]:
        return {
            "name":     self.file_entry.device_path,
            "fileSize": self.file_provider.size,
            "md5":      self.file_provider.md5,
        }

    def _start_log_info(self):
        source = self.file_entry.local_path if self.file_entry.local_path is not None else "<rendered>"
        logging.info(f"File transfer started - Source: {source}")
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

    def _on_connected(self):
        self._send_command()

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
                logging.error(f"Command rejected by device, error code: {message.get('err', 0)}")
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
            self.cleanup()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Sentinels used in the action menu for the fixed (non-devices.yaml) options.
_FW_OPTION = "Firmware upload"
_PROVISION_OPTION = "Initial provisioning (USB: build + upload LittleFS image)"


def select_target(projects: List[ProjectEntry]) -> Optional[ActionResult]:
    """
    Interactive three-level menu:
      1. Select project
      2. Select device
      3. Select action (firmware upload, file transfer, or command)
    Returns an ActionResult, or None if the user cancelled.
    """
    menu = MenuSelector()
    project_map = {p.name: p for p in projects}

    while True:
        # --- Level 1: project selection ---
        choice = menu.select("Select project", list(project_map), show_back=False)
        if choice in (MenuSelector.CANCEL, None):
            return None

        selected_project = project_map[choice]

        while True:
            # --- Level 2: device selection ---
            device_map = {d.display_name: d for d in selected_project.devices}
            choice = menu.select(f"Select device  [{selected_project.name}]", list(device_map), show_back=True)

            if choice in (MenuSelector.CANCEL, None):
                return None
            if choice == MenuSelector.BACK:
                break  # go back to project selection

            selected_device = device_map[choice]

            while True:
                # --- Level 3: action selection ---
                # Order: firmware upload → provisioning → file transfers → commands.
                file_map    = {f.name: f for f in selected_device.files}
                command_map = {c.display_name: c for c in selected_project.commands}
                action_options = [_FW_OPTION, _PROVISION_OPTION] + list(file_map) + list(command_map)

                choice = menu.select(f"Select action  [{selected_device.display_name}]", action_options, show_back=True)

                if choice == MenuSelector.CANCEL:
                    return None
                if choice == MenuSelector.BACK:
                    break  # go back to device selection

                if choice == _FW_OPTION:
                    return ActionResult(project=selected_project, device=selected_device)
                if choice == _PROVISION_OPTION:
                    return ActionResult(project=selected_project, device=selected_device, provision=True)
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
        provider = build_file_provider(result.file, result.device, config_manager)
        if result.file.local_path is not None:
            source = str(result.file.local_path)
        elif result.file.content is not None:
            source = "inline content from devices.yaml"
        else:
            source = "rendered from devices.yaml + secrets.yaml"
        print(f"  Action:      {result.file.name}")
        print(f"  Source:      {source}")
        print(f"  Device path: {result.file.device_path}")
        print()
        return FileTransfer(device_config, mqtt_config, result.file, provider)

    # Firmware upload
    firmware_path = config_manager.get_firmware_path(result.project.pio_project)
    print("  Action:      Firmware upload")
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

        print("\n✅ Configuration loaded successfully:")
        print(f"  Protocol:    {mqtt_config.protocol}")
        print(f"  Host:        {mqtt_config.host}")
        print(f"  Port:        {mqtt_config.port}")
        print(f"  Client ID:   {mqtt_config.client_id}")
        print(f"  TLS Enabled: {mqtt_config.tls_enabled}")
        print(f"  Auth:        {'Yes' if mqtt_config.username else 'No'}")
        print("\n🎯 Target:")
        print(f"  Project:     {result.project.name}  ({result.project.pio_project})")
        print(f"  Device:      {result.device.display_name}")

        if result.provision:
            print("  Action:      Initial provisioning (USB)")
            print()
            provisioner = Provisioner(config_manager.parent_dir, config_manager.pio_command())
            sys.exit(0 if provisioner.provision(result.project, result.device, config_manager) else 1)

        worker = _build_worker(result, config_manager, mqtt_config)

        # Set up signal handler for graceful shutdown
        def signal_handler(sig: int, frame: "FrameType | None") -> None:
            logging.info("Received interrupt signal, shutting down...")
            worker.cleanup()
            sys.exit(0)

        signal.signal(signal.SIGINT, signal_handler)
        if hasattr(signal, 'SIGTERM'):
            signal.signal(signal.SIGTERM, signal_handler)

        sys.exit(0 if worker.run() else 1)

    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(1)
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
