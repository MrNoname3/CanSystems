#!/usr/bin/env python3
"""
ESP8266/ESP32 Over-The-Air (OTA) Update Tool via MQTT

This tool provides a modular approach to updating ESP devices firmware
and transferring configuration files through MQTT communication with
proper error handling and progress tracking.
Uses a YAML secrets file (secrets.yaml, git-ignored) and device list (devices.yaml).
"""

import argparse
import base64
import curses
import enum
import hashlib
import json
import logging
import os
import re
import shutil
import signal
import ssl
import subprocess
import sys
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from types import FrameType
from typing import Any, Callable, ClassVar, Deque, Dict, List, Optional, cast

import paho.mqtt.client as mqtt
import yaml
from paho.mqtt.enums import CallbackAPIVersion
from tqdm import tqdm

# scripts/git_utils.py computes the exact git hash/dirty state the firmware build embeds
# (scripts/git_commit_info.py, the PlatformIO pre-script, calls the same functions); importing it
# rather than reimplementing keeps the reboot verification's "expected" build in lock-step with
# whatever the build actually stamped into the binary.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'scripts'))
import git_utils

# ---------------------------------------------------------------------------
# Enums & Data classes
# ---------------------------------------------------------------------------

# Device-side DataTransferError bit (lib/dataTransfer/src/dataTransfer.hpp): the piece number was
# not the expected one. On a repeated piece it means the device already stored it and only the
# acknowledgment went missing.
WRONG_FILE_PIECE_NUMBER = 1 << 8

# Shared by --ota-timeout and the interactive menu (which has no flag to override it), so a
# firmware upload waits the same length for the device to reboot and confirm the new build
# whichever way it was started.
DEFAULT_REBOOT_TIMEOUT_SECONDS = 60.0
# The pre-upload online check waits on a retained message, which the broker replies with the
# moment the subscription is granted - this only bounds a broker that is slow to answer at all.
PREFLIGHT_ONLINE_TIMEOUT_SECONDS = 10.0

# CAN alert nodes take well under this per device for the transfer, reset and FW_VERSION report -
# deliberate margin. The bus updates every live node of the matching role one after another, so
# the total budget scales with how many actually answered discovery.
CAN_NODE_REBOOT_TIMEOUT_PER_NODE_SECONDS = 45.0
# How long discovery waits for the retained availability/info of every node behind the gateway -
# same reasoning as PREFLIGHT_ONLINE_TIMEOUT_SECONDS, just named for what it is here.
CAN_NODE_DISCOVERY_TIMEOUT_SECONDS = 10.0
# Same kind of wait, for --status: long enough for every device's and every CAN node's retained
# availability/info to answer the wildcard subscription.
FLEET_STATUS_DISCOVERY_TIMEOUT_SECONDS = 10.0


class TransferState(enum.Enum):
    """States for the OTA update / file transfer / command process"""
    IDLE = 0
    WAIT_START_ACK = 1
    SENDING_FW = 2
    WAIT_PIECE_ACK = 3
    WAIT_CHECK_ACK = 4
    DONE = 5
    ERROR = 6


# The two secrets.yaml protocol identifiers - our own choice of spelling, unlike paho's own
# "tcp"/"websockets" transport values below, which are its API and not ours to name. Checked
# again in MQTTClient._setup_client, which is why this is shared rather than local to the class.
_PROTOCOL_MQTT = 'mqtt'
_PROTOCOL_WS = 'ws'


@dataclass
class MQTTConfig:
    """Configuration data for MQTT connection"""
    protocol: str = _PROTOCOL_MQTT             # _PROTOCOL_MQTT or _PROTOCOL_WS
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
        (_PROTOCOL_MQTT, False): 1883,
        (_PROTOCOL_MQTT, True):  8883,
        (_PROTOCOL_WS,   False): 80,
        (_PROTOCOL_WS,   True):  443,
    }

    def __post_init__(self):
        """Validate and set defaults after initialization"""
        if self.protocol not in (_PROTOCOL_MQTT, _PROTOCOL_WS):
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


# The only renderer id `FileEntry.render` currently accepts - checked in three places
# (devices.yaml validation, provider dispatch, the connection-config preflight decision).
_RENDER_SERVER_JSON = 'server_json'


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
    pio_env: Optional[str] = None      # Build environment the file must belong to, checked before sending


@dataclass
class DeviceEntry:
    """A single device entry from devices.yaml"""
    mac: str
    friendly_name: Optional[str] = None
    files: List[FileEntry] = field(default_factory=list[FileEntry])
    server_config: Dict[str, Any] = field(default_factory=dict[str, Any])  # Non-secret server.json fields (e.g. haDiscovery).

    @property
    def display_name(self) -> str:
        """Return the formatted menu label: the friendly name and MAC, or the MAC alone."""
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
    At most one of `file` / `command` is not None, or one of the USB flags is
    True. If none of them is set, the selected action is an OTA firmware upload."""
    project: ProjectEntry
    device: DeviceEntry
    file: Optional[FileEntry] = None        # Set when a file transfer was selected.
    command: Optional[CommandEntry] = None  # Set when a command was selected.
    provision: bool = False                 # Set when USB provisioning was selected.
    serial_flash: bool = False              # Set when the initial USB firmware flash was selected.


# MQTT topic scheme (see README's "MQTT scheme" section) - the one place every topic string
# below is assembled, so a root or field name never needs to be found-and-replaced across the file.
_ROOT_DEVICE_TO_SERVER = 'iot/dtos'
_ROOT_SERVER_TO_DEVICE = 'iot/stod'
_FIELD_COMMON = 'common'
_FIELD_AVAILABILITY = 'availability'
_FIELD_INFO = 'info'

# The availability/info JSON payloads (README: "fw version = git commit count, git hash, dirty
# flag, ..."), read the same way by OTAUpdater, FileTransfer and FleetStatus.
_PAYLOAD_KEY_STATE = 'state'
_PAYLOAD_KEY_GIT = 'git'
_PAYLOAD_KEY_DIRTY = 'dirty'
_STATE_ONLINE = 'online'
_STATE_OFFLINE = 'offline'

# The file-transfer start message (README: "OTA and file transfer") - one schema, sent by both
# OTAUpdater (a firmware image, plus binId) and FileTransfer (any other file).
_START_KEY_NAME = 'name'
_START_KEY_FILE_SIZE = 'fileSize'
_START_KEY_MD5 = 'md5'

# The device's ack/nack reply on its 'common' topic (README: `{"type":1,"cmd":9,"err":0}`) - one
# schema, read by both DataTransfer's piece handshake and CommandSender's reply.
_ACK_KEY_TYPE = 'type'
_ACK_KEY_ERR = 'err'


def _esp_topic(root: str, mac: str, field: str) -> str:
    """A top-level device topic: `<root>/<mac>/<field>`."""
    return f'{root}/{mac}/{field}'


def _can_node_topic(gateway_mac: str, field: str, node: str = '+') -> str:
    """A CAN sub-device topic behind a gateway: `iot/dtos/<gateway_mac>/<node>/<field>`. `node`
    defaults to the single-level wildcard, since every current caller either already knows the one
    node it's watching or is discovering every node there is."""
    return f'{_ROOT_DEVICE_TO_SERVER}/{gateway_mac}/{node}/{field}'


@dataclass
class DeviceConfig:
    """Configuration data for the target device (used by OTAUpdater and FileTransfer)"""
    mac_address: str
    project_name: str

    @property
    def send_topic(self) -> str:
        return _esp_topic(_ROOT_SERVER_TO_DEVICE, self.mac_address, _FIELD_COMMON)

    @property
    def receive_topic(self) -> str:
        return _esp_topic(_ROOT_DEVICE_TO_SERVER, self.mac_address, _FIELD_COMMON)

    @property
    def availability_topic(self) -> str:
        return _esp_topic(_ROOT_DEVICE_TO_SERVER, self.mac_address, _FIELD_AVAILABILITY)

    @property
    def info_topic(self) -> str:
        return _esp_topic(_ROOT_DEVICE_TO_SERVER, self.mac_address, _FIELD_INFO)


# ---------------------------------------------------------------------------
# Device list manager
# ---------------------------------------------------------------------------

# devices.yaml field names read by more than one parser method below - CommandEntry, FileEntry
# and ProjectEntry each have a display 'name', and the command list ('commands') is read once for
# the shared section and once per project, both feeding _parse_commands().
_YAML_KEY_NAME = 'name'
_YAML_KEY_COMMANDS = 'commands'


class DeviceManager:
    """Loads and provides access to the devices.yaml device list"""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.devices_file = self.script_dir / 'devices.yaml'

    def load(self) -> List[ProjectEntry]:
        """Read devices.yaml and return the projects it lists."""
        if not self.devices_file.exists():
            raise FileNotFoundError(
                f"Device list file not found: {self.devices_file}\n"
                f"Please create a devices.yaml file in the same directory as the script."
            )

        try:
            with open(self.devices_file, encoding='utf-8') as f:
                data = yaml.safe_load(f)
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse devices.yaml: {e}") from e

        if not data or 'projects' not in data:
            raise ValueError("devices.yaml must contain a 'projects' key")

        # Parse common commands shared across all projects.
        common_commands = self._parse_commands(
            data.get('common', {}).get(_YAML_KEY_COMMANDS, []),
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
            if _YAML_KEY_NAME not in c or 'cmd' not in c:
                raise ValueError(
                    f"Each command entry must have 'name' and 'cmd' fields (context: {context})"
                )
            commands.append(CommandEntry(
                name=c[_YAML_KEY_NAME],
                cmd=c['cmd'],
                description=c.get('description')
            ))
        return commands

    def _parse_file(self, f: dict[str, Any], mac: str) -> FileEntry:
        """Parse a single file entry dict into a FileEntry object."""
        if _YAML_KEY_NAME not in f or 'device_path' not in f:
            raise ValueError(
                f"Each file entry must have 'name' and 'device_path' fields (device: {mac})"
            )
        sources = [key for key in ('local_path', 'render', 'content') if key in f]
        if len(sources) != 1:
            raise ValueError(
                f"File entry '{f[_YAML_KEY_NAME]}' must have exactly one of 'local_path', 'render' "
                f"or 'content' (device: {mac})"
            )
        if 'render' in f and f['render'] != _RENDER_SERVER_JSON:
            raise ValueError(
                f"Unknown render type '{f['render']}' in file entry '{f[_YAML_KEY_NAME]}' "
                f"(device: {mac}); only '{_RENDER_SERVER_JSON}' is supported"
            )
        if 'content' in f and not isinstance(f['content'], dict):
            raise ValueError(
                f"'content' must be a mapping in file entry '{f[_YAML_KEY_NAME]}' (device: {mac})"
            )
        if 'pio_env' in f and 'local_path' not in f:
            raise ValueError(
                f"'pio_env' names the build a file on disk has to come from, so it only goes with "
                f"'local_path' (file entry '{f[_YAML_KEY_NAME]}', device: {mac})"
            )
        return FileEntry(
            name=f[_YAML_KEY_NAME],
            device_path=f['device_path'],
            local_path=self.script_dir / f['local_path'] if 'local_path' in f else None,
            render=f.get('render'),
            content=cast(Optional[Dict[str, Any]], f.get('content')),
            pio_env=f.get('pio_env')
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
        if _YAML_KEY_NAME not in p or 'pio_project' not in p:
            raise ValueError("Each project entry must have 'name' and 'pio_project' fields")
        # Merge common commands with project-level commands.
        merged_commands = common_commands + self._parse_commands(
            p.get(_YAML_KEY_COMMANDS, []), context=p[_YAML_KEY_NAME])
        return ProjectEntry(
            name=p[_YAML_KEY_NAME],
            pio_project=p['pio_project'],
            commands=merged_commands,
            devices=[self._parse_device(d, p[_YAML_KEY_NAME]) for d in p.get('devices', [])]
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
            with open(self.secrets_file, encoding='utf-8') as file:
                data: Any = yaml.safe_load(file) or {}
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse YAML secrets file: {e}") from e
        except UnicodeDecodeError as e:
            raise ValueError(f"Secrets file encoding error: {e}") from e
        except Exception as e:
            raise OSError(f"Failed to read secrets file: {e}") from e

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
                protocol=broker_data.get('protocol', _PROTOCOL_MQTT),
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
            raise ValueError(f"Configuration validation error: {e}") from e

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

    # The broker CA roots sent to the devices as mosq-ca.crt. Let's Encrypt's
    # ISRG Root X1 + X2 cover both the RSA and the ECDSA issuance chains.
    DEFAULT_CA_ROOTS: ClassVar[List[str]] = ["ISRG Root X1", "ISRG Root X2"]

    def ca_roots(self) -> List[str]:
        """Subject common names of the CA roots the devices must trust:
        the optional top-level 'ca_roots' list of secrets.yaml, else the
        Let's Encrypt defaults."""
        roots: Any = self._load_secrets().get('ca_roots')
        if roots is None:
            return list(self.DEFAULT_CA_ROOTS)
        if not isinstance(roots, list) or not roots \
                or not all(isinstance(r, str) for r in cast(List[Any], roots)):
            raise ValueError("'ca_roots' in secrets.yaml must be a non-empty list of strings")
        return cast(List[str], roots)

    @property
    def ca_bundle_path(self) -> Path:
        """Location of the (git-ignored) CA bundle uploaded to the devices."""
        return self.script_dir / 'mosq-ca.crt'

    def get_firmware_path(self, pio_project: str) -> Path:
        """Get the firmware binary path"""
        firmware_path = self.parent_dir / '.pio' / 'build' / pio_project / 'firmware.bin'
        if not firmware_path.exists():
            raise FileNotFoundError(f"Firmware file not found: {firmware_path}")
        return firmware_path


# ---------------------------------------------------------------------------
# Firmware manager
# ---------------------------------------------------------------------------

def verify_image_environment(data: bytes, pio_env: str, source: str) -> None:
    """Fail unless an image carries the build environment it is being sent as.

    A device compares what it is told against its own BUILD_ENV_NAME, which catches the right
    image going to the wrong node. It cannot catch the wrong image going to the right node - for
    that the file has to be asked what it is, and the build stamps the environment name into every
    image (platformio.ini: -D BUILD_ENV_NAME="$PIOENV"). The CAN nodes have no check of their own
    at all: their firmware travels as an ordinary file and is only checksummed, so this is the
    only place a stale or mismatched image is caught before the bootloader programs it.
    """
    if pio_env.encode('utf-8') + b'\0' not in data:
        raise ValueError(
            f"{source} does not carry the environment name '{pio_env}', "
            f"so it is not that environment's firmware"
        )
    logging.info(f"Firmware ID: \"{pio_env}\"")


class FirmwareManager:
    """Handles firmware file operations and validation"""

    def __init__(self, firmware_path: Path, pio_project: str):
        self.firmware_path = firmware_path
        self.pio_project = pio_project
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
        """The build environment this image belongs to, checked against the image itself."""
        if self._firmware_id is None:
            self._verify_firmware_id()
            self._firmware_id = self.pio_project
        return self._firmware_id

    def _read_firmware(self) -> bytes:
        """Read firmware binary file"""
        try:
            return self.firmware_path.read_bytes()
        except OSError as e:
            raise OSError(f"Failed to read firmware file: {e}") from e

    def _verify_firmware_id(self) -> None:
        """Fail unless the image carries the environment name it is being sent as."""
        verify_image_environment(self.firmware_data, self.pio_project, str(self.firmware_path))


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
            except OSError as e:
                raise OSError(f"Failed to read file: {e}") from e
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
            raise ValueError(f"Invalid JSON file '{self.file_path.name}': {e}") from e

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


def extract_system_ca_roots(root_names: List[str]) -> bytes:
    """Extract the named root certificates (matched by subject commonName)
    from the system trust store as a concatenated PEM bundle, in the order
    given. Raises ValueError when a requested root is not in the store."""
    context = ssl.create_default_context()   # loads the system trust store
    # get_ca_certs() and get_ca_certs(binary_form=True) return index-aligned lists.
    infos = cast(List[Dict[str, Any]], context.get_ca_certs())
    ders = context.get_ca_certs(binary_form=True)
    der_by_name: dict[str, bytes] = {}
    for info, der in zip(infos, ders, strict=True):   # same certs, so index-aligned
        rdns = cast("tuple[tuple[tuple[str, str], ...], ...]", info.get('subject', ()))
        subject = {pair[0]: pair[1] for rdn in rdns for pair in rdn}
        common_name = subject.get('commonName')
        if common_name in root_names and common_name not in der_by_name:
            der_by_name[common_name] = der

    missing = [n for n in root_names if n not in der_by_name]
    if missing:
        raise ValueError(
            f"Root certificate(s) not found in the system trust store: {', '.join(missing)}")

    return ''.join(ssl.DER_cert_to_PEM_cert(der_by_name[n]) for n in root_names).encode('ascii')


def ensure_ca_bundle(config_manager: "ConfigManager") -> Path:
    """Return the CA bundle path, generating the file from the system trust
    store on first use. A hand-placed file is never overwritten."""
    path = config_manager.ca_bundle_path
    if not path.exists():
        roots = config_manager.ca_roots()
        path.write_bytes(extract_system_ca_roots(roots))
        logging.info(f"Generated CA bundle {path} from the system trust store "
                     f"({', '.join(roots)})")
    return path


def build_file_provider(file_entry: FileEntry, device: DeviceEntry,
                        config_manager: "ConfigManager") -> "FileDataProvider | RenderedDataProvider":
    """Data provider for a file entry: disk-backed for local_path entries,
    rendered in memory for render and inline-content entries. A missing CA
    bundle is generated once from the system trust store."""
    if file_entry.render == _RENDER_SERVER_JSON:
        return RenderedDataProvider(render_server_json(
            config_manager.device_server_secrets(device.mac), device.server_config))
    if file_entry.content is not None:
        return RenderedDataProvider(
            json.dumps(file_entry.content, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))
    if file_entry.local_path is None:
        raise ValueError(f"File entry '{file_entry.name}' has no content source")
    if file_entry.local_path == config_manager.ca_bundle_path:
        ensure_ca_bundle(config_manager)
    if not file_entry.local_path.exists():
        raise FileNotFoundError(f"Local file not found: {file_entry.local_path}")
    provider = FileDataProvider(file_entry.local_path)
    if file_entry.pio_env is not None:
        verify_image_environment(provider.data, file_entry.pio_env, str(file_entry.local_path))
    return provider


# ---------------------------------------------------------------------------
# Device-identity preflight check
# ---------------------------------------------------------------------------

def verify_device_connection(server_secrets: Dict[str, Any], cafile: Path, mac: str,
                             timeout: float = 15.0) -> bool:
    """Connect to the broker exactly as the target device would: same host and
    port, the device's own MQTT credentials, plain MQTT over TLS validated
    with the CA bundle that is about to be shipped. Returns True when the
    broker accepts the connection. The client id is 'verify_<mac>' — it
    differs from the device's real client id (so a live device is not kicked
    off its session) while broker logs still show whose identity was tested."""
    host: Any = server_secrets.get('mqttServerUrl')
    port: Any = server_secrets.get('mqttServerPort')
    username: Any = server_secrets.get('mqttUserName')
    password: Any = server_secrets.get('mqttPassword')
    if not all(isinstance(v, str) and v for v in (host, username, password)) \
            or not isinstance(port, int):
        raise ValueError("Device secrets must contain mqttServerUrl/mqttServerPort/"
                         "mqttUserName/mqttPassword for the identity check")

    client = mqtt.Client(
        client_id=f"verify_{mac}",
        callback_api_version=CallbackAPIVersion.VERSION2,
        transport="tcp"
    )
    client.username_pw_set(username=cast(str, username), password=cast(str, password))
    # paho's own tls_set stub leaves some parameters unannotated (partially unknown).
    client.tls_set(ca_certs=str(cafile))  # pyright: ignore[reportUnknownMemberType]

    outcome: List[bool] = []

    def on_connect(cl: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        if reason_code == 0:
            outcome.append(True)
        else:
            logging.error(f"Broker rejected the device credentials: {reason_code}")
            outcome.append(False)

    client.on_connect = on_connect

    logging.info(f"Identity check: connecting to {host}:{port} as '{username}' "
                 f"(TLS via {cafile.name})")
    try:
        client.connect(cast(str, host), port, 30)   # TCP + TLS handshake happen here
    except Exception as e:
        logging.error(f"Identity check failed to reach the broker: {e}")
        return False

    deadline = time.time() + timeout
    while not outcome and time.time() < deadline:
        client.loop(timeout=0.2)
    client.disconnect()

    if not outcome:
        logging.error("Identity check timed out waiting for the broker's CONNACK")
        return False
    if outcome[0]:
        logging.info("Identity check OK: the broker accepts this device's configuration")
    return outcome[0]


def run_identity_check(config_manager: "ConfigManager", device: DeviceEntry) -> bool:
    """Preflight for actions that ship connection config to a device: verify
    the device's rendered identity (credentials + URL/port + CA bundle)
    against the live broker before anything is uploaded or flashed."""
    cafile = ensure_ca_bundle(config_manager)
    secrets = config_manager.device_server_secrets(device.mac)
    print("🔐 Identity check: connecting to the broker as the device would...")
    if not verify_device_connection(secrets, cafile, device.mac):
        print("❌ Identity check failed — nothing was uploaded. "
              "Fix secrets.yaml (or the CA bundle) and retry.")
        return False
    return True


# ---------------------------------------------------------------------------
# USB provisioning (initial LittleFS image)
# ---------------------------------------------------------------------------

class Provisioner:
    """Bench (USB) setup for a factory-fresh device: the initial firmware
    flash and the initial LittleFS provisioning. Running both makes the
    device fully operational without any pre-existing config on it.

    Provisioning materializes every /config/* file entry of the device into
    <repo>/data (the PlatformIO filesystem source directory, git-ignored),
    runs `pio run -e <env> -t uploadfs`, then clears data/ again. This puts
    the device's own credentials on it from the very first flash — no shared
    bootstrap config is involved."""

    CONFIG_PREFIX = '/config/'

    def __init__(self, repo_root: Path, pio_cmd: str, upload_port: Optional[str] = None):
        self.repo_root = repo_root
        self.data_dir = repo_root / 'data'
        self.pio_cmd = pio_cmd
        self.upload_port = upload_port

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
            return self._run_pio_target(project.pio_project, 'uploadfs')
        finally:
            self._clear_data_dir()

    def flash_firmware(self, project: ProjectEntry) -> bool:
        """Build the project's firmware and flash it over serial (initial USB flash)."""
        return self._run_pio_target(project.pio_project, 'upload')

    def _clear_data_dir(self):
        """Reset data/ to an empty directory so only the freshly materialized
        files end up in the filesystem image."""
        if self.data_dir.exists():
            shutil.rmtree(self.data_dir)
        self.data_dir.mkdir()

    def _candidate_ports(self) -> List[str]:
        """The serial ports PlatformIO can see, as "<port>  <description>" lines.
        Diagnostic only: any failure answers with nothing rather than holding up the flash."""
        try:
            listing = subprocess.run([self.pio_cmd, 'device', 'list', '--json-output'],
                                     cwd=self.repo_root, check=False, capture_output=True, text=True)
            entries = cast(List[Dict[str, Any]], json.loads(listing.stdout))
        except (OSError, ValueError):
            return []
        return [f"{e.get('port', '?')}  {e.get('description', '')}".rstrip()
                for e in entries if str(e.get('hwid', 'n/a')) != 'n/a']

    def _warn_if_the_port_is_ambiguous(self):
        """Says so when PlatformIO has more than one board to choose from.

        Not every board manifest carries USB hwids (d1_mini does not), so with several boards
        attached the auto-detection can land on the wrong one - and a serial flash does not ask
        what it is talking to before it writes.
        """
        ports = self._candidate_ports()
        if len(ports) < 2:
            return
        print("⚠  Several serial ports are attached and no --upload-port was given;")
        print("   PlatformIO will pick one of these itself:")
        for port in ports:
            print(f"     {port}")

    def _run_pio_target(self, pio_env: str, target: str) -> bool:
        """Run `pio run -e <env> -t <target>` from the repo root, streaming its output."""
        command = [self.pio_cmd, 'run', '-e', pio_env, '-t', target]
        if self.upload_port is not None:
            command += ['--upload-port', self.upload_port]
        else:
            self._warn_if_the_port_is_ambiguous()
        env = dict(os.environ)
        env['VIRTUAL_ENV'] = ''    # a project .venv confuses pio's own virtualenv detection
        logging.info(f"Running: {' '.join(command)}")
        try:
            result = subprocess.run(command, cwd=self.repo_root, env=env, check=False)
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
        transport = "websockets" if self.config.protocol == _PROTOCOL_WS else "tcp"
        self.client = mqtt.Client(
            client_id=self.config.client_id,
            callback_api_version=CallbackAPIVersion.VERSION2,
            transport=transport
        )

        if self.config.protocol == _PROTOCOL_WS and hasattr(self.client, 'ws_set_options'):
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
        self.max_piece_retries = 3
        self.progress_bar: Optional[tqdm[Any]] = None

        # The piece last put on the wire (number, offset, length), so a lost acknowledgment can
        # be answered by repeating it rather than failing the whole transfer.
        self._last_piece: Optional[tuple[int, int, int]] = None
        self._piece_retries = 0
        # The piece number a repeat is still waiting to be answered for, or None. Kept separate
        # from _piece_retries, which the next accepted piece resets: the device's answer to a
        # repeat can still turn up after a late acknowledgment has already moved the transfer on,
        # and only the number says which piece that answer is really about.
        self._resent_piece: Optional[int] = None

        # Responses are queued rather than acted on where they arrive, so one is handled after
        # the state machine has finished transitioning instead of in the middle of the network
        # event that delivered it. A deque because append and popleft are each atomic, which
        # keeps the drain correct even if this ever moves onto loop_start()'s own thread.
        self._pending_messages: Deque[Dict[str, Any]] = deque()

        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

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
        self._resent_piece = None

    def _process_response(self, message: Dict[str, Any]):
        """Process ACK/NACK response messages from device."""
        if _ACK_KEY_TYPE not in message:
            logging.warning("Received message without 'type' field")
            return

        ack = message[_ACK_KEY_TYPE] != 0
        # The device reports this only for a piece it has already stored, so with a repeat on the
        # wire it says "I have it" rather than "something went wrong".
        answers_a_repeat = (not ack and self._resent_piece is not None
                            and message.get(_ACK_KEY_ERR, 0) == WRONG_FILE_PIECE_NUMBER)
        # ... but it says that about the piece it was sent for, which acknowledges the transfer's
        # position only while that piece is still the one on the wire.
        confirms_the_piece_in_flight = (answers_a_repeat
                                        and self.state == TransferState.WAIT_PIECE_ACK
                                        and self._last_piece is not None
                                        and self._last_piece[0] == self._resent_piece)

        if self.state == TransferState.WAIT_START_ACK and ack:
            logging.info("Start acknowledgment received, beginning transfer")
            self.progress_bar = tqdm(total=self.size, desc=self._progress_desc, unit="B", unit_scale=True)
            self.state = TransferState.SENDING_FW

        elif self.state == TransferState.WAIT_PIECE_ACK and ack:
            self._advance_after_piece()

        elif confirms_the_piece_in_flight:
            logging.info(f"Piece {self._resent_piece} was already stored; the lost acknowledgment is confirmed")
            self._resent_piece = None
            self._advance_after_piece()

        elif self.state == TransferState.WAIT_CHECK_ACK and ack:
            self.state = TransferState.DONE

        elif answers_a_repeat:
            # A late acknowledgment already moved the transfer past the repeated piece, so this
            # names a piece that is no longer the one in flight. Failing here would abort a
            # transfer that the retry had just recovered; acknowledging with it would credit a
            # piece nobody answered.
            logging.info(f"A refusal naming piece {self._resent_piece}, which is behind the one in "
                         f"flight: the repeat's own answer, and it acknowledges nothing")
            self._resent_piece = None

        else:
            logging.error(f"Received NACK in state {self.state}, error code: {message.get(_ACK_KEY_ERR, 0)}")
            self.state = TransferState.ERROR

    def _advance_after_piece(self):
        """Move past the acknowledged piece: send the next one, or finish."""
        self._piece_retries = 0
        if self.remaining_bytes > 0:
            self.state = TransferState.SENDING_FW
        else:
            self._finish_sending()

    def _publish_piece(self, piece_number: int, offset: int, read_size: int):
        """Put one piece on the wire and start waiting for its acknowledgment."""
        self.mqtt_client.publish(self.device_config.send_topic, json.dumps({
            "piece": piece_number,
            "data": base64.b64encode(self.data[offset:offset + read_size]).decode('utf-8')
        }))
        self.state = TransferState.WAIT_PIECE_ACK
        self.timer_start = time.time()

    def _send_piece(self):
        """Send the next data piece to the device."""
        # The device answers pieces in order, so a repeat's own answer can be at most one piece
        # late. Past that, a refusal is the device genuinely out of step and has to fail the
        # transfer rather than be read as an echo of a repeat sent long ago.
        if (self._resent_piece is not None) and (self.piece_number > self._resent_piece + 1):
            self._resent_piece = None
        offset = self.size - self.remaining_bytes
        read_size = min(self.remaining_bytes, self.piece_size)

        self._last_piece = (self.piece_number, offset, read_size)
        self._piece_retries = 0
        self._publish_piece(self.piece_number, offset, read_size)
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
        while self._pending_messages:
            self._process_response(self._pending_messages.popleft())

        if self.state == TransferState.SENDING_FW:
            if self.remaining_bytes > 0:
                self._send_piece()
            else:
                self._finish_sending()

        elif (self.state in {TransferState.WAIT_START_ACK, TransferState.WAIT_PIECE_ACK, TransferState.WAIT_CHECK_ACK}
              and time.time() - self.timer_start > self.timeout_seconds):
            if self.state == TransferState.WAIT_PIECE_ACK and self._piece_retries < self.max_piece_retries and self._last_piece:
                self._piece_retries += 1
                self._resent_piece = self._last_piece[0]
                logging.warning(f"No acknowledgment for piece {self._last_piece[0]}; "
                                f"resending ({self._piece_retries}/{self.max_piece_retries})")
                self._publish_piece(*self._last_piece)
            else:
                logging.error(f"Timeout occurred in state: {self.state.name}")
                self.state = TransferState.ERROR

    def cleanup(self) -> None:
        """Clean up resources."""
        self._close_progress_bar()
        self.mqtt_client.disconnect()

    def _await_preflight(self) -> bool:
        """Runs once connected, before the transfer's own state machine takes over.
        The default needs nothing here: _on_connected() already sent the start message by the
        time this runs. OTAUpdater overrides both to check the device is online first."""
        return True

    def _verify_after_transfer(self) -> bool:
        """Runs once the byte transfer itself reaches DONE. The default has nothing further to
        confirm: the acknowledged MD5 already speaks for a file transfer. OTAUpdater overrides
        this to confirm the device actually rebooted into what was just sent."""
        return True

    def run(self) -> bool:
        """Run the transfer process."""
        if not self.mqtt_client.connect():
            return False

        try:
            if not self._await_preflight():
                return False

            while self.state not in {TransferState.DONE, TransferState.ERROR}:
                self.mqtt_client.loop(timeout=0.1)
                self._process_state()

            success = self.state == TransferState.DONE
            logging.info("Transfer completed successfully" if success else "Transfer failed")
            return success and self._verify_after_transfer()

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
    """Firmware OTA update – sends firmware.bin to the device via MQTT.

    A byte-perfect transfer is not the whole story: the device still has to apply the image and
    come back up running it. Before starting, this refuses to begin against a device that is not
    currently online - a wasted attempt at best. After the transfer, it watches the same session
    for the reboot Connectivity::shutdownMqtt() causes on the device side - an explicit retained
    `{"state":"offline"}` publish before the clean disconnect, then eventually a fresh `info`
    topic naming the build that came back up - and fails if the device never reappears, or
    reappears running something other than what was just sent."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, firmware_path: Path, pio_project: str,
                reboot_timeout: float = DEFAULT_REBOOT_TIMEOUT_SECONDS):
        super().__init__(device_config, mqtt_config)
        self.firmware_manager = FirmwareManager(firmware_path, pio_project)
        self.reboot_timeout = reboot_timeout
        self._connected = False
        self._latest_availability: Optional[str] = None
        self._saw_offline_since_start = False
        self._latest_info: Optional[Dict[str, Any]] = None
        self._info_is_fresh = False  # True once an `info` message has arrived since preflight passed.

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
            _START_KEY_NAME:      "espFirmware",
            _START_KEY_FILE_SIZE: self.firmware_manager.size,
            _START_KEY_MD5:       self.firmware_manager.md5,
            "binId":              self.firmware_manager.firmware_id,
        }

    def _start_log_info(self):
        logging.info(f"OTA started - Size: {self.firmware_manager.size} bytes")
        logging.info(f"  MD5:   {self.firmware_manager.md5}")

    # --- Reboot verification -----------------------------------------------------

    def _on_connected(self) -> None:
        # Subscribed here rather than left to _await_preflight(): both topics are retained, so
        # the broker answers with whatever is current the moment the subscription is granted -
        # which is exactly the pre-upload baseline the preflight check below waits for. The start
        # message itself is *not* sent here, unlike the base class's default: _await_preflight()
        # sends it once the device is confirmed online, not unconditionally on connect.
        self.mqtt_client.subscribe(self.device_config.availability_topic)
        self.mqtt_client.subscribe(self.device_config.info_topic)
        self._connected = True

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
        if msg.topic == self.device_config.availability_topic:
            try:
                state = json.loads(msg.payload.decode()).get(_PAYLOAD_KEY_STATE)
            except json.JSONDecodeError:
                state = None
            self._latest_availability = state
            if state == _STATE_OFFLINE:
                self._saw_offline_since_start = True
            return
        if msg.topic == self.device_config.info_topic:
            try:
                self._latest_info = json.loads(msg.payload.decode())
            except json.JSONDecodeError:
                self._latest_info = None
            self._info_is_fresh = True
            return
        super()._on_message(client, userdata, msg)

    def _await_preflight(self) -> bool:
        deadline = time.time() + PREFLIGHT_ONLINE_TIMEOUT_SECONDS
        while not self._connected and time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
        if not self._connected:
            logging.error("Never connected to the broker; refusing to start the firmware upload")
            return False

        while self._latest_availability is None and time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
        if self._latest_availability != _STATE_ONLINE:
            logging.error(f"Device is not online (last known state: {self._latest_availability!r}); "
                          f"refusing to start a firmware upload against it")
            return False

        # The info topic is retained too, but the broker gives no guarantee about the order - or
        # even the timing - it answers the two subscriptions in. Waiting for it explicitly here
        # drains the value that predates this upload: left undrained, it could otherwise arrive
        # mid-transfer and be mistaken for the device's post-reboot report.
        while not self._info_is_fresh and time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
        if not self._info_is_fresh:
            logging.error(f"No info message arrived within {PREFLIGHT_ONLINE_TIMEOUT_SECONDS:.0f}s; refusing to "
                          f"start without a baseline the post-upload report could be told apart from")
            return False

        logging.info("Device is online; starting the firmware upload")
        # A fresh baseline for the postflight check below: any offline/info message from here on
        # is this upload's own doing, not something left over from before the check ran.
        self._saw_offline_since_start = False
        self._info_is_fresh = False
        self._send_start_message()
        return True

    def _verify_after_transfer(self) -> bool:
        expected_hash = f"{git_utils.get_git_hash():08x}"
        deadline = time.time() + self.reboot_timeout
        confirmed = False
        while time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
            if self._saw_offline_since_start and self._info_is_fresh:
                confirmed = True
                break

        if not confirmed:
            if not self._saw_offline_since_start:
                logging.error(f"Device never went offline within {self.reboot_timeout:.0f}s of the transfer "
                              f"completing; it may not have rebooted into the new firmware")
            else:
                logging.error(f"Device went offline but did not report back within {self.reboot_timeout:.0f}s; "
                              f"it may be stuck rebooting")
            return False

        info = self._latest_info or {}
        actual_hash = info.get(_PAYLOAD_KEY_GIT)
        if actual_hash != expected_hash:
            logging.error(f"Device came back reporting build {actual_hash!r}, expected {expected_hash!r}: "
                          f"the running firmware is not the one just sent")
            return False
        if info.get(_PAYLOAD_KEY_DIRTY):
            logging.warning("The uploaded build was made from a dirty working tree (uncommitted or "
                            "untracked changes) - the git hash matches, but the source it was built from may not")
        logging.info(f"Device rebooted and confirmed running build {expected_hash}")
        return True


# ---------------------------------------------------------------------------
# File transfer (config files, certificates, or any arbitrary file)
# ---------------------------------------------------------------------------

def _match_can_node_topic(topic: str, gateway_mac: str) -> Optional[tuple[str, str]]:
    """If `topic` is `iot/dtos/<gateway_mac>/<node>/availability` or `.../<node>/info`, returns
    (node, field); otherwise None. `node` is whatever subtopic name the node was given at
    commissioning time (e.g. "alert1") - never assumed, always read back off the wire."""
    prefix = f'{_ROOT_DEVICE_TO_SERVER}/{gateway_mac}/'
    if not topic.startswith(prefix):
        return None
    parts = topic[len(prefix):].split('/')
    if len(parts) != 2 or parts[1] not in (_FIELD_AVAILABILITY, _FIELD_INFO):
        return None
    return parts[0], parts[1]


# Keys of a CAN node's tracking state below - not wire fields (nothing on the bus is called
# either of these), just this file's own bookkeeping for "did this node report going down, and
# does it have a fresh info message since the baseline was last reset".
_STATE_SAW_OFFLINE = 'saw_offline'
_STATE_INFO_FRESH = 'info_fresh'


class FileTransfer(_BaseTransfer):
    """Transfers an arbitrary file to the device via MQTT.
    Uses 'name' + 'fileSize' + 'md5' in the start message instead of 'binId',
    which signals to the device that this is a generic file transfer, not a firmware update.

    A `pio_env`-carrying entry additionally means this file is a CAN sub-device image (the CAN
    alert firmware, staged at the gateway and cascaded over the bus - see the repo README's "OTA
    and file transfer" section) - the gateway accepting it says nothing about the CAN nodes it
    then reflashes on their own, over a much slower link. For that case only, the transfer is
    followed by a watch over every live node behind this gateway whose subtopic matches the
    firmware's role (the `pio_env` suffix after its first '_', e.g. "alert" from
    "nanoatmega328_alert"): each such node is expected to go offline and come back reporting the
    same build. Nothing here is configured up front - the node count and the role both come from
    what is actually on the wire, so a node or a whole new role added later needs no change here."""

    def __init__(self, device_config: DeviceConfig, mqtt_config: MQTTConfig, file_entry: FileEntry,
                 provider: "FileDataProvider | RenderedDataProvider | None" = None):
        super().__init__(device_config, mqtt_config)
        self.file_entry = file_entry
        if provider is None:
            if file_entry.local_path is None:
                raise ValueError(f"File entry '{file_entry.name}' needs an explicit provider (rendered content)")
            provider = FileDataProvider(file_entry.local_path)
        self.file_provider = provider
        # Keyed by CAN node subtopic (e.g. "alert1"); only populated when file_entry.pio_env is set.
        self._can_node_state: Dict[str, Dict[str, Any]] = {}

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
            _START_KEY_NAME:      self.file_entry.device_path,
            _START_KEY_FILE_SIZE: self.file_provider.size,
            _START_KEY_MD5:       self.file_provider.md5,
        }

    def _start_log_info(self):
        source = self.file_entry.local_path if self.file_entry.local_path is not None else "<rendered>"
        logging.info(f"File transfer started - Source: {source}")
        logging.info(f"  Device path: {self.file_entry.device_path}")
        logging.info(f"  Size:  {self.file_provider.size} bytes")
        logging.info(f"  MD5:   {self.file_provider.md5}")

    # --- CAN sub-device reboot verification -------------------------------------

    def _on_connected(self) -> None:
        if self.file_entry.pio_env is not None:
            # Watched from the moment the connection is up, not just after the transfer: both
            # topics are retained, and starting early means whatever was already there (the
            # pre-upload baseline) is very likely in hand well before _verify_after_transfer()
            # needs it, rather than racing the CAN-bus transfer for it.
            mac = self.device_config.mac_address
            self.mqtt_client.subscribe(_can_node_topic(mac, _FIELD_AVAILABILITY))
            self.mqtt_client.subscribe(_can_node_topic(mac, _FIELD_INFO))
        super()._on_connected()

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
        match = _match_can_node_topic(msg.topic, self.device_config.mac_address)
        if match is not None:
            node, field = match
            state = self._can_node_state.setdefault(
                node, {_FIELD_AVAILABILITY: None, _FIELD_INFO: None,
                      _STATE_INFO_FRESH: False, _STATE_SAW_OFFLINE: False})
            if field == _FIELD_AVAILABILITY:
                try:
                    value = json.loads(msg.payload.decode()).get(_PAYLOAD_KEY_STATE)
                except json.JSONDecodeError:
                    value = None
                state[_FIELD_AVAILABILITY] = value
                if value == _STATE_OFFLINE:
                    state[_STATE_SAW_OFFLINE] = True
            else:  # _FIELD_INFO
                try:
                    state[_FIELD_INFO] = json.loads(msg.payload.decode())
                except json.JSONDecodeError:
                    state[_FIELD_INFO] = None
                state[_STATE_INFO_FRESH] = True
            return
        super()._on_message(client, userdata, msg)

    def _verify_after_transfer(self) -> bool:
        if self.file_entry.pio_env is None:
            return True  # an ordinary file: the acknowledged MD5 already speaks for it

        role_parts = self.file_entry.pio_env.split('_', 1)
        if len(role_parts) < 2 or not role_parts[1]:
            logging.warning(f"Firmware ID '{self.file_entry.pio_env}' has no '_<role>' suffix to match a CAN "
                            f"node subtopic against, so no node's reboot could be verified")
            return True
        node_role = role_parts[1]

        # Discovery: let whatever is still retained but has not yet arrived catch up, then freeze
        # the set of nodes this upload is expected to have reached - live, of the matching role,
        # and not already offline (the gateway's own OTA cascade skips a node that is, rather than
        # queuing it, so it was never going to be touched by this run).
        discovery_deadline = time.time() + CAN_NODE_DISCOVERY_TIMEOUT_SECONDS
        while time.time() < discovery_deadline:
            self.mqtt_client.loop(timeout=0.1)

        targets = {node: state for node, state in self._can_node_state.items()
                  if node.startswith(node_role) and state[_FIELD_AVAILABILITY] == _STATE_ONLINE}
        if not targets:
            logging.warning(f"No live CAN node behind this gateway matched the role '{node_role}'; "
                            f"nothing to verify")
            return True
        logging.info(f"Watching {len(targets)} CAN node(s) of role '{node_role}': {', '.join(sorted(targets))}")

        # A fresh baseline per node: any offline/info message from here on is this upload's own
        # doing, not the retained value that predates it (drained just above).
        for state in targets.values():
            state[_STATE_SAW_OFFLINE] = False
            state[_STATE_INFO_FRESH] = False

        expected_hash = f"{git_utils.get_git_hash():08x}"
        total_timeout = CAN_NODE_REBOOT_TIMEOUT_PER_NODE_SECONDS * len(targets)
        deadline = time.time() + total_timeout
        while time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
            if all(state[_STATE_SAW_OFFLINE] and state[_STATE_INFO_FRESH] for state in targets.values()):
                break

        all_confirmed = True
        for node in sorted(targets):
            state = targets[node]
            if not (state[_STATE_SAW_OFFLINE] and state[_STATE_INFO_FRESH]):
                logging.error(f"{node}: never confirmed a reboot within the {total_timeout:.0f}s budget "
                              f"(offline seen: {state[_STATE_SAW_OFFLINE]}, info seen: {state[_STATE_INFO_FRESH]})")
                all_confirmed = False
                continue
            info: Dict[str, Any] = state[_FIELD_INFO] or {}
            actual_hash = info.get(_PAYLOAD_KEY_GIT)
            if actual_hash != expected_hash:
                logging.error(f"{node}: came back reporting build {actual_hash!r}, expected {expected_hash!r}: "
                              f"the running firmware is not the one just sent")
                all_confirmed = False
                continue
            if info.get(_PAYLOAD_KEY_DIRTY):
                logging.warning(f"{node}: the uploaded build was made from a dirty working tree (uncommitted "
                                f"or untracked changes) - the git hash matches, but the source it was built "
                                f"from may not")
            logging.info(f"{node}: rebooted and confirmed running build {expected_hash}")
        return all_confirmed


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
        while self._pending_messages:
            message = self._pending_messages.popleft()
            if _ACK_KEY_TYPE not in message:
                logging.warning("Received message without 'type' field")
                continue
            if message[_ACK_KEY_TYPE] != 0:
                self.state = TransferState.DONE
            else:
                logging.error(f"Command rejected by device, error code: {message.get(_ACK_KEY_ERR, 0)}")
                self.state = TransferState.ERROR

        if self.state == TransferState.WAIT_START_ACK and time.time() - self.timer_start > self.timeout_seconds:
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
# Fleet status (read-only: no transfer, so no _BaseTransfer state machine involved)
# ---------------------------------------------------------------------------

class FleetStatus:
    """Reports the current availability/info for every device and CAN sub-device that answers.

    Discovered live, by wildcard, rather than read from devices.yaml: a device not yet listed
    there still shows up, and one that never answers within the window does not - which is
    itself the answer for a device that is off or unreachable."""

    def __init__(self, mqtt_config: MQTTConfig, discovery_timeout: float = FLEET_STATUS_DISCOVERY_TIMEOUT_SECONDS):
        self.mqtt_client = MQTTClient(mqtt_config)
        self.discovery_timeout = discovery_timeout
        self.entries: Dict[tuple[str, Optional[str]], Dict[str, Any]] = {}
        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        if reason_code == 0:
            for topic in (_esp_topic(_ROOT_DEVICE_TO_SERVER, '+', _FIELD_AVAILABILITY),
                         _esp_topic(_ROOT_DEVICE_TO_SERVER, '+', _FIELD_INFO),
                         _can_node_topic('+', _FIELD_AVAILABILITY),
                         _can_node_topic('+', _FIELD_INFO)):
                self.mqtt_client.subscribe(topic)

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
        # <root>/<mac>/<field> (4 parts) or <root>/<mac>/<node>/<field> (5 parts).
        parts = msg.topic.split('/')
        root = '/'.join(parts[:2])
        if root != _ROOT_DEVICE_TO_SERVER:
            return
        if len(parts) == 4:
            mac, node, field = parts[2], None, parts[3]
        elif len(parts) == 5:
            mac, node, field = parts[2], parts[3], parts[4]
        else:
            return
        if field not in (_FIELD_AVAILABILITY, _FIELD_INFO):
            return
        try:
            payload: Optional[Dict[str, Any]] = json.loads(msg.payload.decode())
        except json.JSONDecodeError:
            payload = None
        entry = self.entries.setdefault((mac, node), {_FIELD_AVAILABILITY: None, _FIELD_INFO: None})
        if field == _FIELD_AVAILABILITY:
            entry[_FIELD_AVAILABILITY] = payload.get(_PAYLOAD_KEY_STATE) if payload else None
        else:
            entry[_FIELD_INFO] = payload

    def collect(self) -> Dict[tuple[str, Optional[str]], Dict[str, Any]]:
        """Connects, waits out the discovery window, disconnects, and returns what came in."""
        if not self.mqtt_client.connect():
            return {}
        deadline = time.time() + self.discovery_timeout
        while time.time() < deadline:
            self.mqtt_client.loop(timeout=0.1)
        self.mqtt_client.disconnect()
        return self.entries


def format_fleet_status(entries: Dict[tuple[str, Optional[str]], Dict[str, Any]],
                        projects: List[ProjectEntry]) -> str:
    """One line per discovered device or CAN node: its name if devices.yaml knows the MAC (else
    the MAC itself), online/offline, and its build - flagged against this checkout's own commit,
    the same expected value the reboot verification above already computes."""
    names = {d.mac: (d.friendly_name or d.mac) for p in projects for d in p.devices}
    expected_hash = f"{git_utils.get_git_hash():08x}"

    lines: List[str] = []
    for (mac, node), entry in sorted(entries.items()):
        label = names.get(mac, mac)
        if node is not None:
            label = f"{label} / {node}"
        avail = entry[_FIELD_AVAILABILITY] or "unknown"
        info: Dict[str, Any] = entry[_FIELD_INFO] or {}
        git_hash = info.get(_PAYLOAD_KEY_GIT)
        if git_hash is None:
            build = "no info"
        elif git_hash == expected_hash:
            build = f"{git_hash} (current)"
        else:
            build = f"{git_hash} (outdated, expected {expected_hash})"
        if info.get(_PAYLOAD_KEY_DIRTY):
            build += " [dirty]"
        lines.append(f"{label:35s} {avail:8s} {build}")
    if not lines:
        lines.append("No device answered within the discovery window.")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Sentinels used in the action menu for the fixed (non-devices.yaml) options.
_FW_OPTION = "Firmware upload"
_PROVISION_OPTION = "Initial provisioning (USB: build + upload LittleFS image)"
_SERIAL_FLASH_OPTION = "Initial firmware flash (USB: build + serial upload)"
_FLEET_STATUS_OPTION = "Fleet status (query every device and CAN node)"


def select_target(projects: List[ProjectEntry], mqtt_config: MQTTConfig) -> Optional[ActionResult]:
    """
    Interactive three-level menu:
      1. Select project (or query fleet status, which loops back here)
      2. Select device
      3. Select action (firmware upload, file transfer, or command)
    Returns an ActionResult, or None if the user cancelled.
    """
    menu = MenuSelector()
    project_map = {p.name: p for p in projects}

    while True:
        # --- Level 1: project selection ---
        choice = menu.select("Select project", [_FLEET_STATUS_OPTION, *project_map], show_back=False)
        if choice in (MenuSelector.CANCEL, None):
            return None
        if choice == _FLEET_STATUS_OPTION:
            # curses.wrapper() fully tears down and restores the terminal on each menu.select()
            # call, so plain print()/input() here is safe between two menu turns.
            print(format_fleet_status(FleetStatus(mqtt_config).collect(), projects))
            input("\nPress Enter to continue...")
            continue

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
                # Order: OTA firmware upload → USB provisioning/flash → file transfers → commands.
                file_map    = {f.name: f for f in selected_device.files}
                command_map = {c.display_name: c for c in selected_project.commands}
                action_options = [_FW_OPTION, _PROVISION_OPTION, _SERIAL_FLASH_OPTION,
                                  *file_map, *command_map]

                choice = menu.select(f"Select action  [{selected_device.display_name}]", action_options, show_back=True)

                if choice == MenuSelector.CANCEL:
                    return None
                if choice == MenuSelector.BACK:
                    break  # go back to device selection

                if choice == _FW_OPTION:
                    return ActionResult(project=selected_project, device=selected_device)
                if choice == _PROVISION_OPTION:
                    return ActionResult(project=selected_project, device=selected_device, provision=True)
                if choice == _SERIAL_FLASH_OPTION:
                    return ActionResult(project=selected_project, device=selected_device, serial_flash=True)
                if choice in file_map:
                    return ActionResult(project=selected_project, device=selected_device, file=file_map[choice])
                if choice in command_map:
                    return ActionResult(project=selected_project, device=selected_device, command=command_map[choice])


# ---------------------------------------------------------------------------
# Non-interactive target selection
# ---------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    """Builds the command-line parser. With no arguments at all the interactive menu runs."""
    parser = argparse.ArgumentParser(
        description="OTA update tool. Run without arguments for the interactive menu.",
        epilog="A non-interactive run names its device by MAC and its action in full. Nothing is "
               "defaulted, so a typo fails instead of selecting a target of its own.")
    parser.add_argument('--list', action='store_true',
                        help="print every device and the arguments that select its actions, then exit")
    parser.add_argument('--status', action='store_true',
                        help="query every device and CAN node's current availability/build over MQTT, then exit")
    parser.add_argument('--device', metavar='MAC',
                        help="MAC address of the target device, as devices.yaml spells it")
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--firmware', action='store_true', help="upload this project's firmware over MQTT")
    action.add_argument('--provision', action='store_true', help="initial provisioning over USB")
    action.add_argument('--serial-flash', action='store_true', help="initial firmware flash over USB")
    action.add_argument('--file', metavar='NAME', help="transfer the file entry with this name")
    action.add_argument('--command', metavar='CMD', help="send this command")
    parser.add_argument('--upload-port', metavar='PORT',
                        help="serial port for --provision / --serial-flash; without it PlatformIO "
                             "picks one itself, which is a guess when several boards are attached")
    parser.add_argument('--ota-timeout', type=float, metavar='SECONDS',
                        help="seconds to wait for the device to reboot and confirm the new build "
                             f"after --firmware, overriding the shared default ({DEFAULT_REBOOT_TIMEOUT_SECONDS:.0f}s, "
                             "also what the interactive menu uses)")
    return parser


def format_target_list(projects: List[ProjectEntry]) -> str:
    """Renders every device together with the arguments that select each of its actions.
    The lines are meant to be copied straight onto a command line."""
    lines: List[str] = []
    for project in projects:
        lines.append(f"{project.name}  ({project.pio_project})")
        for device in project.devices:
            label = f"  --device {device.mac}"
            lines.append(f"{label}  # {device.friendly_name}" if device.friendly_name else label)
            lines.append("      --firmware")
            lines.append("      --provision")
            lines.append("      --serial-flash")
            for file_entry in device.files:
                lines.append(f'      --file "{file_entry.name}"')
            for command in project.commands:
                lines.append(f"      --command {command.cmd}")
    return "\n".join(lines)


def resolve_target(projects: List[ProjectEntry], mac: str, *,
                   firmware: bool = False, provision: bool = False, serial_flash: bool = False,
                   file_name: Optional[str] = None, command_name: Optional[str] = None) -> ActionResult:
    """Builds the same ActionResult the menu would, from explicit arguments instead of keystrokes.

    The menu shows what is about to happen before it happens; this path has no such moment, so
    every value has to be named and every mismatch is an error. An unknown MAC, a missing action
    or a name that matches no entry raises rather than falling back on something plausible.
    """
    matches = [(p, d) for p in projects for d in p.devices if d.mac == mac]
    if not matches:
        known = ", ".join(d.mac for p in projects for d in p.devices)
        raise ValueError(f"unknown device '{mac}'; devices.yaml lists: {known}")
    if len(matches) > 1:
        raise ValueError(f"device '{mac}' is listed in more than one project")
    project, device = matches[0]

    given = [name for name, chosen in (('--firmware', firmware),
                                       ('--provision', provision),
                                       ('--serial-flash', serial_flash),
                                       ('--file', file_name is not None),
                                       ('--command', command_name is not None)) if chosen]
    if len(given) != 1:
        raise ValueError("exactly one action is required: --firmware, --provision, --serial-flash, "
                         f"--file NAME or --command CMD (got: {', '.join(given) if given else 'none'})")

    if firmware:
        return ActionResult(project=project, device=device)
    if provision:
        return ActionResult(project=project, device=device, provision=True)
    if serial_flash:
        return ActionResult(project=project, device=device, serial_flash=True)
    if file_name is not None:
        for file_entry in device.files:
            if file_entry.name == file_name:
                return ActionResult(project=project, device=device, file=file_entry)
        known = ", ".join(f'"{f.name}"' for f in device.files) or "none"
        raise ValueError(f"device '{mac}' has no file entry named '{file_name}'; it accepts: {known}")
    for command in project.commands:
        if command.cmd == command_name:
            return ActionResult(project=project, device=device, command=command)
    known = ", ".join(c.cmd for c in project.commands) or "none"
    raise ValueError(f"project '{project.name}' has no command '{command_name}'; it accepts: {known}")


def _build_worker(result: ActionResult, config_manager: ConfigManager, mqtt_config: MQTTConfig,
                  reboot_timeout: float = DEFAULT_REBOOT_TIMEOUT_SECONDS):
    """Factory: create the appropriate worker (OTAUpdater / FileTransfer / CommandSender).
    `reboot_timeout` only matters for the firmware-upload case; every other action ignores it."""
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
    return OTAUpdater(device_config, mqtt_config, firmware_path, result.project.pio_project, reboot_timeout)


def main():
    """Main entry point"""
    parser = build_arg_parser()
    args = parser.parse_args()
    action_given = (bool(args.firmware) or bool(args.provision) or bool(args.serial_flash)
                    or args.file is not None or args.command is not None)
    if args.list and (args.device is not None or action_given or args.upload_port is not None):
        parser.error("--list takes no other arguments")
    if args.status and (args.list or args.device is not None or action_given or args.upload_port is not None
                       or args.ota_timeout is not None):
        parser.error("--status takes no other arguments")
    if args.device is None and action_given:
        parser.error("an action needs --device MAC")
    if args.upload_port is not None and (bool(args.firmware) or args.file is not None or args.command is not None):
        parser.error("--upload-port applies to --provision and --serial-flash only")
    if args.ota_timeout is not None and not args.firmware:
        parser.error("--ota-timeout applies to --firmware only")

    # Configured here rather than in whichever object happens to be built: the USB actions run
    # without any transfer worker, and their progress lines were dropped on the default level.
    logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

    try:
        config_manager = ConfigManager(__file__)
        device_manager = DeviceManager(__file__)

        print("Loading configuration...")
        # The device list is read first so --list answers without needing the broker secrets.
        projects = device_manager.load()

        if args.list:
            print(format_target_list(projects))
            sys.exit(0)

        mqtt_config = config_manager.load_mqtt_config()

        if args.status:
            print(format_fleet_status(FleetStatus(mqtt_config).collect(), projects))
            sys.exit(0)

        if args.device is not None:
            result = resolve_target(projects, str(args.device),
                                    firmware=bool(args.firmware),
                                    provision=bool(args.provision),
                                    serial_flash=bool(args.serial_flash),
                                    file_name=cast(Optional[str], args.file),
                                    command_name=cast(Optional[str], args.command))
        else:
            # Interactive target selection (project → device → action)
            selected = select_target(projects, mqtt_config)
            if selected is None:
                print("Cancelled.")
                sys.exit(0)
            result = selected

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

        # Preflight for actions that ship connection config to the device: the
        # cert and server.json uploads over MQTT and the USB provisioning. All
        # are verified by connecting to the broker with the device's own
        # rendered identity first.
        ships_connection_config = result.provision or (result.file is not None and (
            result.file.render == _RENDER_SERVER_JSON
            or result.file.local_path == config_manager.ca_bundle_path))
        if ships_connection_config and not run_identity_check(config_manager, result.device):
            sys.exit(1)

        if result.provision or result.serial_flash:
            provisioner = Provisioner(config_manager.parent_dir, config_manager.pio_command(),
                                      cast(Optional[str], args.upload_port))
            if result.provision:
                print("  Action:      Initial provisioning (USB)")
                print()
                success = provisioner.provision(result.project, result.device, config_manager)
            else:
                print("  Action:      Initial firmware flash (USB)")
                print()
                success = provisioner.flash_firmware(result.project)
            sys.exit(0 if success else 1)

        reboot_timeout = args.ota_timeout if args.ota_timeout is not None else DEFAULT_REBOOT_TIMEOUT_SECONDS
        worker = _build_worker(result, config_manager, mqtt_config, reboot_timeout)

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
