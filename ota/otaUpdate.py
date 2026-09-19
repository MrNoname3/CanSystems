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
# How long each CAN node behind a restarted gateway has to answer again. The gateway asks every
# node for its FW_VERSION over the bus as it starts and only marks one online once it replies, so
# the budget covers those round trips - counted per node, the bus carrying them one at a time, and
# far shorter than a reflash of the same node.
CAN_NODE_PRESENCE_TIMEOUT_PER_NODE_SECONDS = 30.0
# Same kind of wait, for --status: long enough for every device's and every CAN node's retained
# availability/info to answer the wildcard subscription.
FLEET_STATUS_DISCOVERY_TIMEOUT_SECONDS = 10.0
# After the reboot budget has run out on a device that did go offline, how much longer to listen
# before giving up on hearing why. The verdict is already failure by then; this only buys the
# device's own account of its boot, which beats guessing at what became of it.
REBOOT_DIAGNOSIS_WINDOW_SECONDS = 60.0
# How long an updated device has to stay up before a rollout moves on to the next one. Matches
# Connectivity::onlineSettleTime, which is where the firmware itself decides a link has held.
DEFAULT_SOAK_SECONDS = 300.0
# The retained availability/info/diag all answer the moment the subscription is granted. Draining
# them first is what tells the state left over from the update apart from a fresh drop.
SOAK_BASELINE_SECONDS = 10.0
# How often a soak says it is still going. Long waits otherwise look like a hung script.
SOAK_HEARTBEAT_SECONDS = 60.0
# What --status files an answer under when devices.yaml lists no device with that MAC.
_FLEET_STATUS_UNLISTED_HEADING = "Not in devices.yaml"

# How long one network turn blocks before the caller gets to re-check its own state. Every wait
# below is a loop of these, so a deadline is honoured to about this much.
MQTT_LOOP_INTERVAL_SECONDS = 0.1


class TransferState(enum.Enum):
    """States for the OTA update / file transfer / command process"""
    IDLE = 0
    WAIT_START_ACK = 1
    SENDING_FW = 2
    WAIT_PIECE_ACK = 3
    WAIT_CHECK_ACK = 4
    DONE = 5
    ERROR = 6


# The two secrets.yaml protocol identifiers, and the paho transport values they map to.
_PROTOCOL_MQTT = 'mqtt'
_PROTOCOL_WS = 'ws'
_TRANSPORT_TCP = 'tcp'
_TRANSPORT_WEBSOCKETS = 'websockets'


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
            raise ValueError(f"Unsupported protocol: {self.protocol}. "
                             f"Must be '{_PROTOCOL_MQTT}' or '{_PROTOCOL_WS}'")

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


# The only renderer id `FileEntry.render` accepts.
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
class RolloutStep:
    """One device's place in the rollout order, from devices.yaml's `rollout` section.

    `before_firmware` is sent first, in the order given, and the device's own firmware last: the
    CAN alert image travels through the gateway, so it goes while that gateway is still running
    the build it was proven on."""
    project: ProjectEntry
    device: DeviceEntry
    soak_seconds: float = DEFAULT_SOAK_SECONDS
    reboot_timeout: float = DEFAULT_REBOOT_TIMEOUT_SECONDS
    before_firmware: List[FileEntry] = field(default_factory=list[FileEntry])


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


# MQTT topic scheme (see README's "MQTT scheme" section): every topic below is built from these.
_ROOT_DEVICE_TO_SERVER = 'iot/dtos'
_ROOT_SERVER_TO_DEVICE = 'iot/stod'
_FIELD_COMMON = 'common'
_FIELD_AVAILABILITY = 'availability'
_FIELD_INFO = 'info'
_FIELD_DIAG = 'diag'
_TOPIC_WILDCARD = '+'  # MQTT's single-level wildcard, standing in for one MAC or node subtopic

# The availability/info JSON payloads (README: "fw version = git commit count, git hash, dirty
# flag, ...").
_PAYLOAD_KEY_STATE = 'state'
_PAYLOAD_KEY_GIT = 'git'
_PAYLOAD_KEY_DIRTY = 'dirty'
_STATE_ONLINE = 'online'
_STATE_OFFLINE = 'offline'
# `info` is published once per startup, so a second one means the device restarted; its `boot`
# field names the stage the run before it reached (BootStage in lib/bootProgress).
_INFO_KEY_BOOT = 'boot'
# The reset that started the run reporting this info; what the numbers mean is the platform's
# business (ResetHandler::getResetReason), which is why they are passed on rather than read here.
_INFO_KEY_RESET_REASON = 'rr'
# `diag` is published on every offline->online transition, so one arriving says the link dropped.
_DIAG_KEY_CAUSE = 'cause'
_DIAG_KEY_RECONNECTS = 'n'

# The file-transfer start message (README: "OTA and file transfer"), sent by both OTAUpdater (a
# firmware image, which alone carries the bin id) and FileTransfer (any other file), followed by
# the piece messages that carry the content itself.
_START_KEY_NAME = 'name'
_START_KEY_FILE_SIZE = 'fileSize'
_START_KEY_MD5 = 'md5'
_START_KEY_BIN_ID = 'binId'
_PIECE_KEY_NUMBER = 'piece'
_PIECE_KEY_DATA = 'data'

# The device's ack/nack reply on its 'common' topic (README: `{"type":1,"cmd":9,"err":0}`), read
# by the piece handshake and by CommandSender, which sends its command under that same 'cmd' name.
_ACK_KEY_TYPE = 'type'
_ACK_KEY_ERR = 'err'
_COMMAND_KEY_CMD = 'cmd'
# Warned about by both readers of that field.
_MISSING_ACK_FIELD_WARNING = f"Received message without '{_ACK_KEY_TYPE}' field"


def _expected_build_hash() -> str:
    """The build this checkout would put on a device, spelled the way `info` reports it."""
    return f"{git_utils.get_git_hash():08x}"


def _confirm_reported_build(info: Dict[str, Any], expected_hash: str, subject: str) -> bool:
    """Reads one info report back against the build just sent, for whoever `subject` names - the
    device itself after a firmware upload, or a CAN node after the gateway cascaded one to it.
    A hash that does not match fails; a dirty build only warns, the hash being right either way."""
    actual_hash = info.get(_PAYLOAD_KEY_GIT)
    if actual_hash != expected_hash:
        logging.error(f"{subject}: came back reporting build {actual_hash!r}, expected "
                      f"{expected_hash!r}: the running firmware is not the one just sent")
        return False
    if info.get(_PAYLOAD_KEY_DIRTY):
        logging.warning(f"{subject}: the uploaded build was made from a dirty working tree "
                        f"(uncommitted or untracked changes) - the git hash matches, but the "
                        f"source it was built from may not")
    logging.info(f"{subject}: rebooted and confirmed running build {expected_hash}")
    return True


def _esp_topic(root: str, mac: str, field: str) -> str:
    """A top-level device topic: `<root>/<mac>/<field>`."""
    return f'{root}/{mac}/{field}'


def _can_node_topic(gateway_mac: str, field: str, node: str = _TOPIC_WILDCARD) -> str:
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

# devices.yaml's field names, as the _parse_* methods below check for them and read them back.
_YAML_KEY_NAME = 'name'
_YAML_KEY_CMD = 'cmd'
_YAML_KEY_DESCRIPTION = 'description'
_YAML_KEY_COMMANDS = 'commands'
_YAML_KEY_DEVICE_PATH = 'device_path'
_YAML_KEY_LOCAL_PATH = 'local_path'
_YAML_KEY_RENDER = 'render'
_YAML_KEY_CONTENT = 'content'
_YAML_KEY_PIO_ENV = 'pio_env'
_YAML_KEY_MAC = 'mac'
_YAML_KEY_FRIENDLY_NAME = 'friendly_name'
_YAML_KEY_FILES = 'files'
_YAML_KEY_SERVER_CONFIG = 'server_config'
_YAML_KEY_PIO_PROJECT = 'pio_project'
_YAML_KEY_DEVICES = 'devices'
_YAML_KEY_PROJECTS = 'projects'
_YAML_KEY_ROLLOUT = 'rollout'
_YAML_KEY_STEPS = 'steps'
_YAML_KEY_SOAK_SECONDS = 'soak_seconds'
_YAML_KEY_REBOOT_TIMEOUT = 'reboot_timeout'
_YAML_KEY_BEFORE_FIRMWARE = 'before_firmware'
# The devices.yaml section of commands shared by every project, and the label a validation
# error reports it by.
_YAML_COMMON_SECTION = 'common'
# The device list this manager reads.
_DEVICES_FILE_NAME = 'devices.yaml'

# What a listing prints where a device accepts no files or a project defines no commands.
_NOTHING_LISTED = 'none'


class DeviceManager:
    """Loads and provides access to the devices.yaml device list"""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.devices_file = self.script_dir / _DEVICES_FILE_NAME
        # Filled by load(); parse_rollout() needs the projects it returns to resolve its MACs.
        self._rollout_raw: dict[str, Any] = {}

    def load(self) -> List[ProjectEntry]:
        """Read devices.yaml and return the projects it lists."""
        if not self.devices_file.exists():
            raise FileNotFoundError(
                f"Device list file not found: {self.devices_file}\n"
                f"Please create a {_DEVICES_FILE_NAME} file in the same directory as the script."
            )

        try:
            with open(self.devices_file, encoding='utf-8') as f:
                data = yaml.safe_load(f)
        except yaml.YAMLError as e:
            raise ValueError(f"Failed to parse {_DEVICES_FILE_NAME}: {e}") from e

        if not data or _YAML_KEY_PROJECTS not in data:
            raise ValueError(f"{_DEVICES_FILE_NAME} must contain a '{_YAML_KEY_PROJECTS}' key")

        # Parse common commands shared across all projects.
        common_commands = self._parse_commands(
            data.get(_YAML_COMMON_SECTION, {}).get(_YAML_KEY_COMMANDS, []),
            context=_YAML_COMMON_SECTION
        )

        projects = [self._parse_project(p, common_commands) for p in data[_YAML_KEY_PROJECTS]]

        if not projects:
            raise ValueError(f"{_DEVICES_FILE_NAME} contains no projects")

        rollout: Any = data.get(_YAML_KEY_ROLLOUT, {})
        if not isinstance(rollout, dict):
            raise ValueError(f"'{_YAML_KEY_ROLLOUT}' must be a mapping in {_DEVICES_FILE_NAME}")
        self._rollout_raw = cast(dict[str, Any], rollout)

        return projects

    def parse_rollout(self, projects: List[ProjectEntry]) -> List[RolloutStep]:
        """The rollout order, resolved against the projects load() returned.

        Every value is named or inherited from the section default; nothing is guessed. A MAC no
        project lists, the same MAC twice, or a file entry the device does not accept raises here,
        where the whole order is still on screen, rather than part way through a fleet."""
        raw_steps: Any = self._rollout_raw.get(_YAML_KEY_STEPS, [])
        if not isinstance(raw_steps, list):
            raise ValueError(f"'{_YAML_KEY_ROLLOUT}.{_YAML_KEY_STEPS}' must be a list "
                             f"in {_DEVICES_FILE_NAME}")
        default_soak = float(self._rollout_raw.get(_YAML_KEY_SOAK_SECONDS, DEFAULT_SOAK_SECONDS))
        default_reboot = float(self._rollout_raw.get(_YAML_KEY_REBOOT_TIMEOUT,
                                                     DEFAULT_REBOOT_TIMEOUT_SECONDS))

        by_mac = {d.mac: (p, d) for p in projects for d in p.devices}
        steps: List[RolloutStep] = []
        seen: set[str] = set()
        for raw in cast(List[Any], raw_steps):
            if not isinstance(raw, dict):
                raise ValueError(f"Each {_YAML_KEY_ROLLOUT} step must be a mapping "
                                 f"with a '{_YAML_KEY_MAC}' field")
            step = cast(dict[str, Any], raw)
            mac = step.get(_YAML_KEY_MAC)
            if mac is None:
                raise ValueError(f"Each {_YAML_KEY_ROLLOUT} step must have a '{_YAML_KEY_MAC}' field")
            if mac not in by_mac:
                known = ", ".join(by_mac)
                raise ValueError(f"{_YAML_KEY_ROLLOUT} step names unknown device '{mac}'; "
                                 f"{_DEVICES_FILE_NAME} lists: {known}")
            if mac in seen:
                raise ValueError(f"{_YAML_KEY_ROLLOUT} names device '{mac}' more than once")
            seen.add(cast(str, mac))
            project, device = by_mac[mac]
            steps.append(RolloutStep(
                project=project,
                device=device,
                soak_seconds=float(step.get(_YAML_KEY_SOAK_SECONDS, default_soak)),
                reboot_timeout=float(step.get(_YAML_KEY_REBOOT_TIMEOUT, default_reboot)),
                before_firmware=self._parse_before_firmware(step, device),
            ))
        return steps

    @staticmethod
    def _parse_before_firmware(step: dict[str, Any], device: DeviceEntry) -> List[FileEntry]:
        """The file entries a step sends ahead of the device's own firmware, by name."""
        raw: Any = step.get(_YAML_KEY_BEFORE_FIRMWARE, [])
        if not isinstance(raw, list):
            raise ValueError(f"'{_YAML_KEY_BEFORE_FIRMWARE}' must be a list of file entry names "
                             f"(device: {device.mac})")
        by_name = {f.name: f for f in device.files}
        entries: List[FileEntry] = []
        for name in cast(List[Any], raw):
            if name not in by_name:
                known = ", ".join(f'"{n}"' for n in by_name) or _NOTHING_LISTED
                raise ValueError(f"'{_YAML_KEY_BEFORE_FIRMWARE}' names '{name}', which device "
                                 f"'{device.mac}' has no file entry for; it accepts: {known}")
            entries.append(by_name[cast(str, name)])
        return entries

    def _parse_commands(self, raw: list[Any], context: str) -> List[CommandEntry]:
        """Parse a list of raw command dicts into CommandEntry objects."""
        commands: list[CommandEntry] = []
        for c in raw:
            if _YAML_KEY_NAME not in c or _YAML_KEY_CMD not in c:
                raise ValueError(
                    f"Each command entry must have '{_YAML_KEY_NAME}' and '{_YAML_KEY_CMD}' "
                    f"fields (context: {context})"
                )
            commands.append(CommandEntry(
                name=c[_YAML_KEY_NAME],
                cmd=c[_YAML_KEY_CMD],
                description=c.get(_YAML_KEY_DESCRIPTION)
            ))
        return commands

    def _parse_file(self, f: dict[str, Any], mac: str) -> FileEntry:
        """Parse a single file entry dict into a FileEntry object."""
        if _YAML_KEY_NAME not in f or _YAML_KEY_DEVICE_PATH not in f:
            raise ValueError(
                f"Each file entry must have '{_YAML_KEY_NAME}' and '{_YAML_KEY_DEVICE_PATH}' "
                f"fields (device: {mac})"
            )
        sources = [key for key in (_YAML_KEY_LOCAL_PATH, _YAML_KEY_RENDER, _YAML_KEY_CONTENT) if key in f]
        if len(sources) != 1:
            raise ValueError(
                f"File entry '{f[_YAML_KEY_NAME]}' must have exactly one of '{_YAML_KEY_LOCAL_PATH}', "
                f"'{_YAML_KEY_RENDER}' or '{_YAML_KEY_CONTENT}' (device: {mac})"
            )
        if _YAML_KEY_RENDER in f and f[_YAML_KEY_RENDER] != _RENDER_SERVER_JSON:
            raise ValueError(
                f"Unknown render type '{f[_YAML_KEY_RENDER]}' in file entry '{f[_YAML_KEY_NAME]}' "
                f"(device: {mac}); only '{_RENDER_SERVER_JSON}' is supported"
            )
        if _YAML_KEY_CONTENT in f and not isinstance(f[_YAML_KEY_CONTENT], dict):
            raise ValueError(
                f"'{_YAML_KEY_CONTENT}' must be a mapping in file entry "
                f"'{f[_YAML_KEY_NAME]}' (device: {mac})"
            )
        if _YAML_KEY_PIO_ENV in f and _YAML_KEY_LOCAL_PATH not in f:
            raise ValueError(
                f"'{_YAML_KEY_PIO_ENV}' names the build a file on disk has to come from, so it only "
                f"goes with '{_YAML_KEY_LOCAL_PATH}' (file entry '{f[_YAML_KEY_NAME]}', device: {mac})"
            )
        return FileEntry(
            name=f[_YAML_KEY_NAME],
            device_path=f[_YAML_KEY_DEVICE_PATH],
            local_path=self.script_dir / f[_YAML_KEY_LOCAL_PATH] if _YAML_KEY_LOCAL_PATH in f else None,
            render=f.get(_YAML_KEY_RENDER),
            content=cast(Optional[Dict[str, Any]], f.get(_YAML_KEY_CONTENT)),
            pio_env=f.get(_YAML_KEY_PIO_ENV)
        )

    def _parse_device(self, d: dict[str, Any], project_name: str) -> DeviceEntry:
        """Parse a single device entry dict into a DeviceEntry object."""
        if _YAML_KEY_MAC not in d:
            raise ValueError(f"Each device entry must have a '{_YAML_KEY_MAC}' field "
                             f"(project: {project_name})")
        server_config: Any = d.get(_YAML_KEY_SERVER_CONFIG, {})
        if not isinstance(server_config, dict):
            raise ValueError(f"'{_YAML_KEY_SERVER_CONFIG}' must be a mapping "
                             f"(device: {d[_YAML_KEY_MAC]})")
        return DeviceEntry(
            mac=d[_YAML_KEY_MAC],
            friendly_name=d.get(_YAML_KEY_FRIENDLY_NAME),
            files=[self._parse_file(f, d[_YAML_KEY_MAC]) for f in d.get(_YAML_KEY_FILES, [])],
            server_config=cast(Dict[str, Any], server_config)
        )

    def _parse_project(self, p: dict[str, Any], common_commands: List[CommandEntry]) -> ProjectEntry:
        """Parse a single project entry dict into a ProjectEntry object."""
        if _YAML_KEY_NAME not in p or _YAML_KEY_PIO_PROJECT not in p:
            raise ValueError(f"Each project entry must have '{_YAML_KEY_NAME}' and "
                             f"'{_YAML_KEY_PIO_PROJECT}' fields")
        # Merge common commands with project-level commands.
        merged_commands = common_commands + self._parse_commands(
            p.get(_YAML_KEY_COMMANDS, []), context=p[_YAML_KEY_NAME])
        return ProjectEntry(
            name=p[_YAML_KEY_NAME],
            pio_project=p[_YAML_KEY_PIO_PROJECT],
            commands=merged_commands,
            devices=[self._parse_device(d, p[_YAML_KEY_NAME]) for d in p.get(_YAML_KEY_DEVICES, [])]
        )


# ---------------------------------------------------------------------------
# Interactive curses menu
# ---------------------------------------------------------------------------

class MenuSelector:
    """Arrow-key driven interactive menu using curses"""

    # Return sentinels
    BACK = "__BACK__"
    CANCEL = "__CANCEL__"

    # The navigation entries' own labels: appended to the option list, then matched against
    # whichever entry was highlighted.
    BACK_LABEL = "← Back"
    CANCEL_LABEL = "✕ Cancel"

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
        nav_items = ([self.BACK_LABEL] if show_back else []) + [self.CANCEL_LABEL]
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
                if selected == self.CANCEL_LABEL:
                    return self.CANCEL
                if selected == self.BACK_LABEL:
                    return self.BACK
                return selected
            elif key == 27:  # Escape
                return self.CANCEL


# ---------------------------------------------------------------------------
# Config manager
# ---------------------------------------------------------------------------

# secrets.yaml's own top-level sections, and the file name the messages about them point to.
_SECRETS_FILE_NAME = 'secrets.yaml'
_SECRETS_KEY_BROKER = 'broker'
_SECRETS_KEY_SERVER_DEFAULTS = 'server_defaults'
_SECRETS_KEY_DEVICES = 'devices'
_SECRETS_KEY_PIO = 'pio'
_SECRETS_KEY_CA_ROOTS = 'ca_roots'
# The PlatformIO executable the 'pio' override stands in for: looked for in the penv, then on PATH.
_PIO_EXECUTABLE = 'pio'


class ConfigManager:
    """Loads ota/secrets.yaml: the tool's broker connection, the per-device
    server.json secrets, and the optional pio executable override used by
    USB provisioning. secrets.yaml is git-ignored — it is the single file
    carried over manually when the repo is cloned on another machine."""

    def __init__(self, script_path: str):
        self.script_dir = Path(script_path).parent
        self.parent_dir = self.script_dir.parent
        self.secrets_file = self.script_dir / _SECRETS_FILE_NAME
        self._secrets: Optional[dict[str, Any]] = None

    def _load_secrets(self) -> dict[str, Any]:
        """Load and cache secrets.yaml."""
        if self._secrets is not None:
            return self._secrets

        if not self.secrets_file.exists():
            raise FileNotFoundError(
                f"Secrets file not found: {self.secrets_file}\n"
                f"Create ota/{_SECRETS_FILE_NAME} from the template in ota/README.md "
                f"(it is git-ignored)."
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
            raise ValueError(f"{_SECRETS_FILE_NAME} must be a YAML mapping")

        self._secrets = cast(dict[str, Any], data)
        return self._secrets

    @staticmethod
    def _require_mapping(value: Any, key: str) -> dict[str, Any]:
        """Every secrets.yaml section is a mapping, and says so the same way when it is not."""
        if not isinstance(value, dict):
            raise ValueError(f"'{key}' in {_SECRETS_FILE_NAME} must be a mapping")
        return cast(dict[str, Any], value)

    def load_mqtt_config(self) -> MQTTConfig:
        """Build the tool's broker connection from the 'broker' section of secrets.yaml."""
        broker_data = self._require_mapping(self._load_secrets().get(_SECRETS_KEY_BROKER),
                                            _SECRETS_KEY_BROKER)

        # A relative cafile is resolved against ota/, so the tool works from any CWD.
        cafile: Optional[str] = broker_data.get('cafile')
        if cafile is not None and not Path(cafile).expanduser().is_absolute():
            cafile = str(self.script_dir / cafile)

        try:
            # client_id is the one field with a default of its own; the rest fall back to
            # MQTTConfig's.
            return MQTTConfig(
                protocol=broker_data.get('protocol', MQTTConfig.protocol),
                host=broker_data.get('host', MQTTConfig.host),
                port=broker_data.get('port', MQTTConfig.port),
                basepath=broker_data.get('basepath', MQTTConfig.basepath),
                client_id=broker_data.get('client_id', "OtaUpdater"),
                username=broker_data.get('username'),
                password=broker_data.get('password'),
                tls_enabled=broker_data.get('tls_enabled', MQTTConfig.tls_enabled),
                cafile=cafile
            )
        except (ValueError, FileNotFoundError) as e:
            raise ValueError(f"Configuration validation error: {e}") from e

    def device_server_secrets(self, mac: str) -> dict[str, Any]:
        """Secret server.json fields for one device: 'server_defaults' merged
        with (and overridden by) the device's entry under 'devices'."""
        data = self._load_secrets()

        defaults = self._require_mapping(data.get(_SECRETS_KEY_SERVER_DEFAULTS) or {},
                                         _SECRETS_KEY_SERVER_DEFAULTS)
        devices = self._require_mapping(data.get(_SECRETS_KEY_DEVICES) or {}, _SECRETS_KEY_DEVICES)

        entry: Any = devices.get(mac)
        if entry is None:
            raise ValueError(f"No entry for device {mac} under '{_SECRETS_KEY_DEVICES}' "
                             f"in {_SECRETS_FILE_NAME}")
        if not isinstance(entry, dict):
            raise ValueError(f"Device entry {mac} in {_SECRETS_FILE_NAME} must be a mapping")

        return {**defaults, **cast(dict[str, Any], entry)}

    def pio_command(self) -> str:
        """The pio executable used for provisioning: the optional top-level 'pio'
        key of secrets.yaml, else the standard PlatformIO penv location when it
        exists, else 'pio' from PATH."""
        override: Any = self._load_secrets().get(_SECRETS_KEY_PIO)
        if override:
            return str(Path(str(override)).expanduser())
        bundled = Path.home() / '.platformio' / 'penv' / 'bin' / _PIO_EXECUTABLE
        return str(bundled) if bundled.exists() else _PIO_EXECUTABLE

    # The broker CA roots sent to the devices as mosq-ca.crt. Let's Encrypt's
    # ISRG Root X1 + X2 cover both the RSA and the ECDSA issuance chains.
    DEFAULT_CA_ROOTS: ClassVar[List[str]] = ["ISRG Root X1", "ISRG Root X2"]

    def ca_roots(self) -> List[str]:
        """Subject common names of the CA roots the devices must trust:
        the optional top-level 'ca_roots' list of secrets.yaml, else the
        Let's Encrypt defaults."""
        roots: Any = self._load_secrets().get(_SECRETS_KEY_CA_ROOTS)
        if roots is None:
            return list(self.DEFAULT_CA_ROOTS)
        if not isinstance(roots, list) or not roots \
                or not all(isinstance(r, str) for r in cast(List[Any], roots)):
            raise ValueError(f"'{_SECRETS_KEY_CA_ROOTS}' in {_SECRETS_FILE_NAME} must be a "
                             f"non-empty list of strings")
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

# The four server.json fields a device cannot connect without, also read one by one by the
# identity preflight below.
_SERVER_JSON_KEY_USERNAME = "mqttUserName"
_SERVER_JSON_KEY_PASSWORD = "mqttPassword"
_SERVER_JSON_KEY_URL = "mqttServerUrl"
_SERVER_JSON_KEY_PORT = "mqttServerPort"

# Every field server.json may carry, in the order the rendered content emits them.
_SERVER_JSON_REQUIRED = (_SERVER_JSON_KEY_USERNAME, _SERVER_JSON_KEY_PASSWORD,
                         _SERVER_JSON_KEY_URL, _SERVER_JSON_KEY_PORT)
_SERVER_JSON_FIELDS = (*_SERVER_JSON_REQUIRED, "haDiscovery", "ssid", "password")


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
    host: Any = server_secrets.get(_SERVER_JSON_KEY_URL)
    port: Any = server_secrets.get(_SERVER_JSON_KEY_PORT)
    username: Any = server_secrets.get(_SERVER_JSON_KEY_USERNAME)
    password: Any = server_secrets.get(_SERVER_JSON_KEY_PASSWORD)
    if not all(isinstance(v, str) and v for v in (host, username, password)) \
            or not isinstance(port, int):
        raise ValueError(f"Device secrets must contain {'/'.join(_SERVER_JSON_REQUIRED)} "
                         f"for the identity check")

    client = mqtt.Client(
        client_id=f"verify_{mac}",
        callback_api_version=CallbackAPIVersion.VERSION2,
        transport=_TRANSPORT_TCP
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

# What `pio device list --json-output` reports for a port it could not identify.
_PIO_HWID_UNKNOWN = 'n/a'


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
                for e in entries if str(e.get('hwid', _PIO_HWID_UNKNOWN)) != _PIO_HWID_UNKNOWN]

    def _warn_if_the_port_is_ambiguous(self):
        """Says so when PlatformIO has more than one board to choose from.

        Not every board manifest carries USB hwids (d1_mini does not), so with several boards
        attached the auto-detection can land on the wrong one - and a serial flash does not ask
        what it is talking to before it writes.
        """
        ports = self._candidate_ports()
        if len(ports) < 2:
            return
        print(f"⚠  Several serial ports are attached and no {_FLAG_UPLOAD_PORT} was given;")
        print("   PlatformIO will pick one of these itself:")
        for port in ports:
            print(f"     {port}")

    def _run_pio_target(self, pio_env: str, target: str) -> bool:
        """Run `pio run -e <env> -t <target>` from the repo root, streaming its output."""
        command = [self.pio_cmd, 'run', '-e', pio_env, '-t', target]
        if self.upload_port is not None:
            command += ['--upload-port', self.upload_port]  # pio's own flag, not this tool's
        else:
            self._warn_if_the_port_is_ambiguous()
        env = dict(os.environ)
        env['VIRTUAL_ENV'] = ''    # a project .venv confuses pio's own virtualenv detection
        logging.info(f"Running: {' '.join(command)}")
        try:
            result = subprocess.run(command, cwd=self.repo_root, env=env, check=False)
        except FileNotFoundError:
            logging.error(f"pio executable not found: {self.pio_cmd} "
                          f"(set the '{_SECRETS_KEY_PIO}' key in {_SECRETS_FILE_NAME})")
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
        transport = _TRANSPORT_WEBSOCKETS if self.config.protocol == _PROTOCOL_WS else _TRANSPORT_TCP
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

    def loop(self, timeout: float = MQTT_LOOP_INTERVAL_SECONDS):
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
            logging.warning(_MISSING_ACK_FIELD_WARNING)
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
            _PIECE_KEY_NUMBER: piece_number,
            _PIECE_KEY_DATA:   base64.b64encode(self.data[offset:offset + read_size]).decode('utf-8')
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
                self.mqtt_client.loop()
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
            _START_KEY_BIN_ID:    self.firmware_manager.firmware_id,
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
            self.mqtt_client.loop()
        if not self._connected:
            logging.error("Never connected to the broker; refusing to start the firmware upload")
            return False

        while self._latest_availability is None and time.time() < deadline:
            self.mqtt_client.loop()
        if self._latest_availability != _STATE_ONLINE:
            logging.error(f"Device is not online (last known state: {self._latest_availability!r}); "
                          f"refusing to start a firmware upload against it")
            return False

        # The info topic is retained too, but the broker gives no guarantee about the order - or
        # even the timing - it answers the two subscriptions in. Waiting for it explicitly here
        # drains the value that predates this upload: left undrained, it could otherwise arrive
        # mid-transfer and be mistaken for the device's post-reboot report.
        while not self._info_is_fresh and time.time() < deadline:
            self.mqtt_client.loop()
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
        expected_hash = _expected_build_hash()
        deadline = time.time() + self.reboot_timeout
        confirmed = False
        while time.time() < deadline:
            self.mqtt_client.loop()
            if self._saw_offline_since_start and self._info_is_fresh:
                confirmed = True
                break

        if not confirmed:
            if not self._saw_offline_since_start:
                logging.error(f"Device never went offline within {self.reboot_timeout:.0f}s of the transfer "
                              f"completing; it may not have rebooted into the new firmware")
                return False
            # It did restart, so it is the coming back that is late. Keep listening a while longer,
            # not to change the verdict but to let the device say what happened to its boot: a
            # late report carries the reset that started it and how far the run before it got.
            diagnosis_deadline = time.time() + REBOOT_DIAGNOSIS_WINDOW_SECONDS
            while time.time() < diagnosis_deadline and not self._info_is_fresh:
                self.mqtt_client.loop()
            logging.error(self._late_reboot_report())
            return False

        return _confirm_reported_build(self._latest_info or {}, expected_hash, "Device")

    def _late_reboot_report(self) -> str:
        """What to say about a device that went offline and did not report back in time.

        The values are passed on as the device spelled them: what a reset reason means differs
        between the parts this runs against, and a table here would be a second, staler copy of
        an enum that already lives in the firmware."""
        if not self._info_is_fresh:
            waited = self.reboot_timeout + REBOOT_DIAGNOSIS_WINDOW_SECONDS
            return (f"Device went offline and had still not reported back {waited:.0f}s after the "
                    f"transfer; it may be stuck rebooting")
        info: Dict[str, Any] = self._latest_info or {}
        return (f"Device reported back only after its {self.reboot_timeout:.0f}s budget had run out, "
                f"running build {info.get(_PAYLOAD_KEY_GIT, 'unknown')}, reset reason "
                f"{info.get(_INFO_KEY_RESET_REASON, 'unknown')}, the run before it having reached "
                f"boot stage {info.get(_INFO_KEY_BOOT, 'unknown')} "
                f"(ResetHandler::getResetReason and BootStage name these)")


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


# Keys of a CAN node's tracking state below - this file's own bookkeeping, not wire fields:
# whether the node has been seen offline, and whether its info is newer than the baseline.
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
        # What the CAN cascade reached, and what it could not because the node was not live to be
        # reached. Both stay empty for an ordinary file, which has no nodes behind it.
        self.nodes_confirmed: List[str] = []
        self.nodes_missed: List[str] = []

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
            self.mqtt_client.loop()

        targets = {node: state for node, state in self._can_node_state.items()
                  if node.startswith(node_role) and state[_FIELD_AVAILABILITY] == _STATE_ONLINE}
        # Named rather than passed over silently: a node that was off got no firmware, and nothing
        # later in a rollout would come back to it or say that it had been left behind.
        self.nodes_missed = sorted(node for node, state in self._can_node_state.items()
                                   if node.startswith(node_role) and state[_FIELD_AVAILABILITY] != _STATE_ONLINE)
        if self.nodes_missed:
            logging.warning(f"CAN node(s) of role '{node_role}' not live, so not reflashed by this "
                            f"upload: {', '.join(self.nodes_missed)}")
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

        expected_hash = _expected_build_hash()
        total_timeout = CAN_NODE_REBOOT_TIMEOUT_PER_NODE_SECONDS * len(targets)
        deadline = time.time() + total_timeout
        while time.time() < deadline:
            self.mqtt_client.loop()
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
            if not _confirm_reported_build(info, expected_hash, node):
                all_confirmed = False
                continue
            self.nodes_confirmed.append(node)
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
        self.mqtt_client.publish(self.device_config.send_topic,
                                 json.dumps({_COMMAND_KEY_CMD: self.command.cmd}))
        logging.info(f"Command sent: '{self.command.cmd}'")
        self.state = TransferState.WAIT_START_ACK
        self.timer_start = time.time()

    def _process_state(self):
        """Process pending messages and check for timeout."""
        while self._pending_messages:
            message = self._pending_messages.popleft()
            if _ACK_KEY_TYPE not in message:
                logging.warning(_MISSING_ACK_FIELD_WARNING)
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
                self.mqtt_client.loop()
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
            for topic in (_esp_topic(_ROOT_DEVICE_TO_SERVER, _TOPIC_WILDCARD, _FIELD_AVAILABILITY),
                         _esp_topic(_ROOT_DEVICE_TO_SERVER, _TOPIC_WILDCARD, _FIELD_INFO),
                         _can_node_topic(_TOPIC_WILDCARD, _FIELD_AVAILABILITY),
                         _can_node_topic(_TOPIC_WILDCARD, _FIELD_INFO)):
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
            self.mqtt_client.loop()
        self.mqtt_client.disconnect()
        return self.entries


def _format_fleet_build(entry: Dict[str, Any], expected_hash: str) -> str:
    """The build half of one status line: what the device reports, against what this checkout is."""
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
    return build


def format_fleet_status(entries: Dict[tuple[str, Optional[str]], Dict[str, Any]],
                        projects: List[ProjectEntry]) -> str:
    """Everything that answered, grouped as --list and the menu group it: by project, then by
    device, with a gateway's CAN nodes indented under it. A device is named as the menu names it,
    friendly name and MAC together, so a line can be acted on without looking the address up
    again. What answered from a MAC devices.yaml does not list keeps a heading of its own."""
    expected_hash = _expected_build_hash()

    nodes_by_mac: Dict[str, List[str]] = {}
    for mac, node in entries:
        if node is not None:
            nodes_by_mac.setdefault(mac, []).append(node)

    # (label, entry) per line; entry is None for a heading, and for a gateway that did not answer
    # itself but has nodes to introduce.
    def device_rows(mac: str, label: str) -> List[tuple[str, Optional[Dict[str, Any]]]]:
        rows: List[tuple[str, Optional[Dict[str, Any]]]] = [(f"  {label}", entries.get((mac, None)))]
        rows += [(f"    {node}", entries[(mac, node)]) for node in sorted(nodes_by_mac.get(mac, []))]
        return rows

    rows: List[tuple[str, Optional[Dict[str, Any]]]] = []
    for project in projects:
        answered = [d for d in project.devices if (d.mac, None) in entries or d.mac in nodes_by_mac]
        if not answered:
            continue
        rows.append((f"{project.name}  ({project.pio_project})", None))
        for device in answered:
            rows += device_rows(device.mac, device.display_name)

    listed = {d.mac for p in projects for d in p.devices}
    unlisted = sorted({mac for mac, _ in entries if mac not in listed})
    if unlisted:
        rows.append((_FLEET_STATUS_UNLISTED_HEADING, None))
        for mac in unlisted:
            rows += device_rows(mac, mac)

    if not rows:
        return "No device answered within the discovery window."

    # Only the lines that carry columns set their width; a heading is free to be longer.
    width = max(len(label) for label, entry in rows if entry is not None)
    lines: List[str] = []
    for label, entry in rows:
        if entry is None:
            # A heading starts a new block; an indented label is a gateway that did not answer
            # itself and is only here to say whose nodes follow.
            if lines and not label.startswith(" "):
                lines.append("")
            lines.append(label)
            continue
        avail = entry[_FIELD_AVAILABILITY] or "unknown"
        lines.append(f"{label:{width}s}  {avail:8s} {_format_fleet_build(entry, expected_hash)}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Fleet rollout
# ---------------------------------------------------------------------------

class StepStatus(enum.Enum):
    """Where a rollout step stands: the first three are decided before anything is sent, the last
    three by the run itself."""
    PENDING = "pending"
    SKIPPED_OFFLINE = "skipped: offline"
    SKIPPED_CURRENT = "skipped: already current"
    DONE = "done"
    FAILED = "failed"
    NOT_REACHED = "not reached"


@dataclass
class PlannedStep:
    """One line of the plan, and the same line again in the summary once the run has touched it."""
    step: RolloutStep
    status: StepStatus
    reported: Optional[str] = None   # The build the device answered discovery with, if any.
    detail: str = ""                 # Why a step failed, or what is worth saying about one that did not.
    nodes: List[str] = field(default_factory=list[str])  # CAN nodes that answered discovery online.


def _entry_is_current(entry: Optional[Dict[str, Any]], expected_hash: str) -> bool:
    """Whether what a device (or node) reported is this checkout's build, cleanly built."""
    info: Dict[str, Any] = (entry or {}).get(_FIELD_INFO) or {}
    return info.get(_PAYLOAD_KEY_GIT) == expected_hash and not info.get(_PAYLOAD_KEY_DIRTY)


def build_rollout_plan(steps: List[RolloutStep],
                       entries: Dict[tuple[str, Optional[str]], Dict[str, Any]],
                       expected_hash: str) -> List[PlannedStep]:
    """Decides, from one fleet snapshot, what each step of the order has left to do.

    A device that did not answer discovery is passed over rather than allowed to stop the run: one
    switched off should not hold up the rest of the fleet, and the summary says it was left out. A
    step counts as current only when its CAN nodes are current too, so a gateway whose nodes were
    left behind is still picked up."""
    planned: List[PlannedStep] = []
    for step in steps:
        entry = entries.get((step.device.mac, None))
        info: Dict[str, Any] = (entry or {}).get(_FIELD_INFO) or {}
        reported = cast(Optional[str], info.get(_PAYLOAD_KEY_GIT))
        if entry is None or entry.get(_FIELD_AVAILABILITY) != _STATE_ONLINE:
            planned.append(PlannedStep(step, StepStatus.SKIPPED_OFFLINE, reported))
            continue
        nodes = {node: e for (mac, node), e in entries.items()
                 if mac == step.device.mac and node is not None}
        # Only the ones answering online count, in both decisions below: a node that is off cannot
        # be reached by this run, so holding the step pending for it would re-send the gateway's
        # firmware on every rollout and still not update the node. It is picked up by the first
        # run that finds it back on the bus.
        live = sorted(node for node, e in nodes.items() if e.get(_FIELD_AVAILABILITY) == _STATE_ONLINE)
        if _entry_is_current(entry, expected_hash) and all(_entry_is_current(nodes[n], expected_hash) for n in live):
            planned.append(PlannedStep(step, StepStatus.SKIPPED_CURRENT, reported, nodes=live))
            continue
        planned.append(PlannedStep(step, StepStatus.PENDING, reported, nodes=live))
    return planned


def _rollout_rows(planned: List[PlannedStep], expected_hash: str) -> List[tuple[str, str, str, str]]:
    """(place and project, device, status, detail) per step, with each step's pre-firmware
    transfers under it.

    The order runs across projects rather than through one at a time, so each line carries its
    own - which is also what tells two devices of the same friendly name apart."""
    rows: List[tuple[str, str, str, str]] = []
    for index, entry in enumerate(planned, start=1):
        step = entry.step
        was = entry.reported or "no info"
        if entry.status is StepStatus.DONE:
            # What it is on now, not what discovery found before the run: the old hash alone reads
            # as though the step had changed nothing.
            detail = f"{was} -> {expected_hash}"
            if entry.detail:
                detail += f"; {entry.detail}"
        elif entry.detail:
            detail = entry.detail
        elif entry.status is StepStatus.PENDING:
            detail = f"{was} -> soak {step.soak_seconds:.0f}s"
        else:
            detail = entry.reported or ""
        rows.append((f"  {index}. {step.project.name}", step.device.display_name,
                     entry.status.value, detail))
        rows += [("", f"  + {f.name}", "", "") for f in step.before_firmware]
    return rows


def _format_rollout_rows(heading: str, planned: List[PlannedStep], expected_hash: str) -> str:
    """Both listings share this: one column set, sized to whatever the longest entry needs."""
    if not planned:
        return f"{heading}\n  (no steps; {_DEVICES_FILE_NAME} lists no {_YAML_KEY_ROLLOUT} order)"
    rows = _rollout_rows(planned, expected_hash)
    place_width = max(len(place) for place, _, _, _ in rows)
    device_width = max(len(device) for _, device, _, _ in rows)
    status_width = max(len(status) for _, _, status, _ in rows)
    lines = [heading, ""]
    for place, device, status, detail in rows:
        lines.append(f"{place:{place_width}s}  {device:{device_width}s}  "
                     f"{status:{status_width}s}  {detail}".rstrip())
    return "\n".join(lines)


def format_rollout_plan(planned: List[PlannedStep], expected_hash: str) -> str:
    """What the run is about to do, printed before it is allowed to do any of it."""
    return _format_rollout_rows(f"Rollout plan  (expected build {expected_hash})", planned, expected_hash)


def format_rollout_summary(planned: List[PlannedStep], expected_hash: str) -> str:
    """The same rows once the run has finished with them, however far it got."""
    return _format_rollout_rows("Rollout summary", planned, expected_hash)


def _can_cascade_notes(transfer: FileTransfer) -> List[str]:
    """What a CAN firmware upload is worth saying about in the summary though it did not fail.

    A node that was off is not an error - the gateway's cascade skips it rather than queuing it -
    but it is still a node left on the old image, and the summary is the only place that would
    ever say so."""
    if transfer.file_entry.pio_env is None:
        return []
    notes: List[str] = []
    if transfer.nodes_missed:
        notes.append(f"offline, not reflashed: {', '.join(transfer.nodes_missed)}")
    if not transfer.nodes_confirmed:
        notes.append(f"no CAN node answered {transfer.file_entry.name}")
    return notes


def _run_rollout_step(entry: PlannedStep, config_manager: "ConfigManager",
                      mqtt_config: MQTTConfig) -> Optional[str]:
    """Sends one step: its pre-firmware transfers first, then the device's own image, each
    followed by the soak that has to hold before anything else is sent. Returns None when the
    whole step held, or what stopped it."""
    step = entry.step
    device_config = DeviceConfig(mac_address=step.device.mac, project_name=step.project.pio_project)
    notes: List[str] = []

    for file_entry in step.before_firmware:
        logging.info(f"Rollout: {step.device.display_name} - {file_entry.name}")
        provider = build_file_provider(file_entry, step.device, config_manager)
        transfer = FileTransfer(device_config, mqtt_config, file_entry, provider)
        if not transfer.run():
            return f"{file_entry.name} did not complete"
        notes += _can_cascade_notes(transfer)
        # The nodes it reflashed have to hold as well, so the gateway is watched with them.
        held = SoakWatcher(mqtt_config, step.device.mac, step.soak_seconds, watch_nodes=True).run()
        if held is not None:
            return held

    logging.info(f"Rollout: {step.device.display_name} - firmware")
    firmware_path = config_manager.get_firmware_path(step.project.pio_project)
    updater = OTAUpdater(device_config, mqtt_config, firmware_path,
                         step.project.pio_project, step.reboot_timeout)
    if not updater.run():
        return "firmware upload did not complete"
    # Without the nodes: the gateway's own restart takes their presence with it, so watching them
    # through this soak would fail on a reboot that went perfectly well. They are checked after it.
    held = SoakWatcher(mqtt_config, step.device.mac, step.soak_seconds).run()
    if held is not None:
        return held
    returned = CanNodePresence(mqtt_config, step.device.mac, entry.nodes).run()
    if returned is not None:
        return returned
    entry.detail = "; ".join(notes)
    return None


def run_rollout(planned: List[PlannedStep], config_manager: "ConfigManager",
                mqtt_config: MQTTConfig) -> bool:
    """Walks the plan, updating each step's status as it goes.

    Stops at the first failure and marks whatever was still to come as not reached. There is no
    rollback on any board here, so a build that fails one device is a build that must not be sent
    to the next; carrying on would multiply the devices needing a cable."""
    for index, entry in enumerate(planned):
        if entry.status is not StepStatus.PENDING:
            continue
        failure = _run_rollout_step(entry, config_manager, mqtt_config)
        if failure is None:
            entry.status = StepStatus.DONE
            continue
        entry.status = StepStatus.FAILED
        entry.detail = failure
        for later in planned[index + 1:]:
            if later.status is StepStatus.PENDING:
                later.status = StepStatus.NOT_REACHED
        return False
    return True


class CanNodePresence:
    """Confirms the CAN nodes behind a gateway answered again after the gateway itself restarted.

    CanMqttGateway::init() republishes every node's availability as offline on each start, and
    only marks one online again once that node has replied to the FW_VERSION it asks for over the
    bus. So this reads whether the CAN side came back at all - which a gateway build that comes up
    on MQTT, holds its soak and never touches the bus again would otherwise pass.

    Presence only, in both senses: the nodes' own firmware is not what a gateway update changes,
    so what build they report is the business of the transfer that put it there - and this returns
    as soon as they are all back rather than watching them stay, which is what the soak before it
    does for the gateway itself."""

    def __init__(self, mqtt_config: MQTTConfig, mac: str, nodes: List[str],
                 timeout_per_node: float = CAN_NODE_PRESENCE_TIMEOUT_PER_NODE_SECONDS):
        self.mqtt_client = MQTTClient(mqtt_config)
        self.mac = mac
        self.expected = set(nodes)
        self.timeout = timeout_per_node * len(self.expected)
        self.online: set[str] = set()
        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        if reason_code == 0:
            self.mqtt_client.subscribe(_can_node_topic(self.mac, _FIELD_AVAILABILITY))

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
        match = _match_can_node_topic(msg.topic, self.mac)
        if match is None or match[1] != _FIELD_AVAILABILITY:
            return
        try:
            state = json.loads(msg.payload.decode()).get(_PAYLOAD_KEY_STATE)
        except (json.JSONDecodeError, UnicodeDecodeError):
            state = None
        if state == _STATE_ONLINE:
            self.online.add(match[0])
        else:
            self.online.discard(match[0])

    def run(self) -> Optional[str]:
        """Blocks until every expected node is back. Returns None then, or which ones are missing."""
        if not self.expected:
            return None
        if not self.mqtt_client.connect():
            return f"could not reach the broker to check the CAN nodes behind {self.mac}"
        try:
            logging.info(f"CAN nodes: waiting for {', '.join(sorted(self.expected))} to answer again")
            deadline = time.time() + self.timeout
            while time.time() < deadline and not self.expected <= self.online:
                self.mqtt_client.loop()
            missing = sorted(self.expected - self.online)
            if missing:
                return (f"CAN node(s) behind {self.mac} did not come back within "
                        f"{self.timeout:.0f}s: {', '.join(missing)}")
            logging.info("CAN nodes: all back")
            return None
        finally:
            self.mqtt_client.disconnect()


class SoakWatcher:
    """Watches one device hold its connection for a while after an update.

    A firmware upload that confirms the new build has proven the device came back once. It has not
    proven it stayed: a fault in the main loop shows up seconds or minutes later, by which time an
    unattended rollout has moved on. So this waits - not by sleeping, but by watching for the
    evidence a drop leaves on the broker. Any of the three ends the wait early and names itself,
    which is why a bad build costs seconds here rather than the whole window.

    A gateway's CAN nodes are watched with it when asked: they answer availability and info, but
    not diag, which belongs to the ESP-side Connectivity."""

    def __init__(self, mqtt_config: MQTTConfig, mac: str, soak_seconds: float,
                 watch_nodes: bool = False, baseline_timeout: float = SOAK_BASELINE_SECONDS):
        self.mqtt_client = MQTTClient(mqtt_config)
        self.mac = mac
        self.soak_seconds = soak_seconds
        self.watch_nodes = watch_nodes
        self.baseline_timeout = baseline_timeout
        self.availability: Optional[str] = None
        # Until the retained baseline is drained, a message says what the update left behind; after
        # it, the same message says the device has moved since.
        self.baseline_done = False
        self.failure: Optional[str] = None
        self.mqtt_client.set_callbacks(self._on_connect, self._on_message)

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any) -> None:
        if reason_code != 0:
            return
        for topic_field in (_FIELD_AVAILABILITY, _FIELD_INFO, _FIELD_DIAG):
            self.mqtt_client.subscribe(_esp_topic(_ROOT_DEVICE_TO_SERVER, self.mac, topic_field))
        if self.watch_nodes:
            for topic_field in (_FIELD_AVAILABILITY, _FIELD_INFO):
                self.mqtt_client.subscribe(_can_node_topic(self.mac, topic_field))

    def _on_message(self, client: Any, userdata: Any, msg: Any) -> None:
        try:
            payload: Dict[str, Any] = json.loads(msg.payload.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            payload = {}
        node_match = _match_can_node_topic(msg.topic, self.mac)
        if node_match is not None:
            subject, topic_field = f"{self.mac}/{node_match[0]}", node_match[1]
        else:
            subject, topic_field = self.mac, msg.topic.rsplit('/', 1)[-1]
        if topic_field == _FIELD_AVAILABILITY and node_match is None:
            self.availability = payload.get(_PAYLOAD_KEY_STATE)
        if not self.baseline_done:
            return
        self._record_failure(subject, topic_field, payload)

    def _record_failure(self, subject: str, topic_field: str, payload: Dict[str, Any]) -> None:
        """The first signal to arrive after the baseline is the one reported; later ones are its
        aftermath and would only bury it."""
        if self.failure is not None:
            return
        if topic_field == _FIELD_AVAILABILITY:
            if payload.get(_PAYLOAD_KEY_STATE) == _STATE_OFFLINE:
                self.failure = f"{subject} went offline during the soak"
        elif topic_field == _FIELD_INFO:
            stage = payload.get(_INFO_KEY_BOOT)
            reached = f"; the run before it reached boot stage {stage}" if stage is not None else ""
            self.failure = f"{subject} restarted during the soak{reached}"
        elif topic_field == _FIELD_DIAG:
            cause = payload.get(_DIAG_KEY_CAUSE)
            count = payload.get(_DIAG_KEY_RECONNECTS)
            detail = f" (cause {cause}, reconnect #{count})" if cause is not None else ""
            self.failure = f"{subject} dropped its connection during the soak{detail}"

    def run(self) -> Optional[str]:
        """Blocks for the soak window. Returns None when the device held it, or what ended it."""
        if not self.mqtt_client.connect():
            return f"could not reach the broker to watch {self.mac}"
        try:
            deadline = time.time() + self.baseline_timeout
            while time.time() < deadline:
                self.mqtt_client.loop()
            if self.availability != _STATE_ONLINE:
                return (f"{self.mac} is not online as the soak starts "
                        f"(last known state: {self.availability!r})")
            self.baseline_done = True
            logging.info(f"Soak: watching {self.mac} for {self.soak_seconds:.0f}s")

            end = time.time() + self.soak_seconds
            next_beat = time.time() + SOAK_HEARTBEAT_SECONDS
            while time.time() < end:
                self.mqtt_client.loop()
                if self.failure is not None:
                    return self.failure
                if time.time() >= next_beat:
                    # Clamped: the loop() this follows can carry the clock past the deadline the
                    # turn before it was checked, and a countdown does not go below zero.
                    logging.info(f"Soak: {self.mac} holding, {max(0.0, end - time.time()):.0f}s left")
                    next_beat += SOAK_HEARTBEAT_SECONDS
            logging.info(f"Soak: {self.mac} held for {self.soak_seconds:.0f}s")
            return None
        finally:
            self.mqtt_client.disconnect()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Sentinels used in the action menu for the fixed (non-devices.yaml) options.
_FW_OPTION = "Firmware upload"
_PROVISION_OPTION = "Initial provisioning (USB: build + upload LittleFS image)"
_SERIAL_FLASH_OPTION = "Initial firmware flash (USB: build + serial upload)"
_FLEET_STATUS_OPTION = "Fleet status (query every device and CAN node)"
_ROLLOUT_OPTION = "Fleet rollout (update every device in the configured order)"


def select_target(projects: List[ProjectEntry], mqtt_config: MQTTConfig,
                  rollout_action: Callable[[], None]) -> Optional[ActionResult]:
    """
    Interactive three-level menu:
      1. Select project (or query fleet status / run the rollout, which loop back here)
      2. Select device
      3. Select action (firmware upload, file transfer, or command)
    Returns an ActionResult, or None if the user cancelled. `rollout_action` runs the whole
    configured order; it is passed in rather than built here so this stays about the choosing.
    """
    menu = MenuSelector()
    project_map = {p.name: p for p in projects}

    while True:
        # --- Level 1: project selection ---
        choice = menu.select("Select project", [_FLEET_STATUS_OPTION, _ROLLOUT_OPTION, *project_map],
                             show_back=False)
        if choice in (MenuSelector.CANCEL, None):
            return None
        if choice == _FLEET_STATUS_OPTION:
            # curses.wrapper() fully tears down and restores the terminal on each menu.select()
            # call, so plain print()/input() here is safe between two menu turns.
            print(format_fleet_status(FleetStatus(mqtt_config).collect(), projects))
            input("\nPress Enter to continue...")
            continue
        if choice == _ROLLOUT_OPTION:
            rollout_action()
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

# The command line's own flag names: the parser is built from these, --list prints them, and the
# validation messages name them.
_FLAG_LIST = '--list'
_FLAG_STATUS = '--status'
_FLAG_DEVICE = '--device'
_FLAG_FIRMWARE = '--firmware'
_FLAG_PROVISION = '--provision'
_FLAG_SERIAL_FLASH = '--serial-flash'
_FLAG_FILE = '--file'
_FLAG_COMMAND = '--command'
_FLAG_UPLOAD_PORT = '--upload-port'
_FLAG_OTA_TIMEOUT = '--ota-timeout'
_FLAG_ROLLOUT = '--rollout'
_FLAG_DRY_RUN = '--dry-run'
_FLAG_YES = '--yes'
# The placeholders those flags take an argument under, repeated wherever a message spells out an
# invocation.
_METAVAR_MAC = 'MAC'
_METAVAR_NAME = 'NAME'
_METAVAR_CMD = 'CMD'
_METAVAR_PORT = 'PORT'
_METAVAR_SECONDS = 'SECONDS'
# Said of --list and --status, each of which answers on its own.
_TAKES_NO_OTHER_ARGUMENTS = "takes no other arguments"
# --rollout names no target of its own; these two are the only flags that go with it.
_ROLLOUT_COMPANION_FLAGS = f"{_FLAG_DRY_RUN} and {_FLAG_YES}"


def build_arg_parser() -> argparse.ArgumentParser:
    """Builds the command-line parser. With no arguments at all the interactive menu runs."""
    parser = argparse.ArgumentParser(
        description="OTA update tool. Run without arguments for the interactive menu.",
        epilog="A non-interactive run names its device by MAC and its action in full. Nothing is "
               "defaulted, so a typo fails instead of selecting a target of its own.")
    parser.add_argument(_FLAG_LIST, action='store_true',
                        help="print every device and the arguments that select its actions, then exit")
    parser.add_argument(_FLAG_STATUS, action='store_true',
                        help="query every device and CAN node's current availability/build over MQTT, then exit")
    parser.add_argument(_FLAG_DEVICE, metavar=_METAVAR_MAC,
                        help=f"MAC address of the target device, as {_DEVICES_FILE_NAME} spells it")
    action = parser.add_mutually_exclusive_group()
    action.add_argument(_FLAG_FIRMWARE, action='store_true', help="upload this project's firmware over MQTT")
    action.add_argument(_FLAG_PROVISION, action='store_true', help="initial provisioning over USB")
    action.add_argument(_FLAG_SERIAL_FLASH, action='store_true', help="initial firmware flash over USB")
    action.add_argument(_FLAG_FILE, metavar=_METAVAR_NAME, help="transfer the file entry with this name")
    action.add_argument(_FLAG_COMMAND, metavar=_METAVAR_CMD, help="send this command")
    parser.add_argument(_FLAG_UPLOAD_PORT, metavar=_METAVAR_PORT,
                        help=f"serial port for {_FLAG_PROVISION} / {_FLAG_SERIAL_FLASH}; without it "
                             f"PlatformIO picks one itself, which is a guess when several boards "
                             f"are attached")
    parser.add_argument(_FLAG_ROLLOUT, action='store_true',
                        help=f"update every device the {_YAML_KEY_ROLLOUT} section of "
                             f"{_DEVICES_FILE_NAME} lists, in the order it gives, waiting for each "
                             f"to hold its connection before starting the next")
    parser.add_argument(_FLAG_DRY_RUN, action='store_true',
                        help=f"with {_FLAG_ROLLOUT}, print the plan and exit without sending anything")
    parser.add_argument(_FLAG_YES, action='store_true',
                        help=f"with {_FLAG_ROLLOUT}, start without asking to confirm the plan")
    parser.add_argument(_FLAG_OTA_TIMEOUT, type=float, metavar=_METAVAR_SECONDS,
                        help=f"seconds to wait for the device to reboot and confirm the new build "
                             f"after {_FLAG_FIRMWARE}, overriding the shared default "
                             f"({DEFAULT_REBOOT_TIMEOUT_SECONDS:.0f}s, also what the interactive "
                             f"menu uses)")
    return parser


def format_target_list(projects: List[ProjectEntry]) -> str:
    """Renders every device together with the arguments that select each of its actions.
    The lines are meant to be copied straight onto a command line."""
    lines: List[str] = []
    for project in projects:
        lines.append(f"{project.name}  ({project.pio_project})")
        for device in project.devices:
            label = f"  {_FLAG_DEVICE} {device.mac}"
            lines.append(f"{label}  # {device.friendly_name}" if device.friendly_name else label)
            lines.append(f"      {_FLAG_FIRMWARE}")
            lines.append(f"      {_FLAG_PROVISION}")
            lines.append(f"      {_FLAG_SERIAL_FLASH}")
            for file_entry in device.files:
                lines.append(f'      {_FLAG_FILE} "{file_entry.name}"')
            for command in project.commands:
                lines.append(f"      {_FLAG_COMMAND} {command.cmd}")
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
        raise ValueError(f"unknown device '{mac}'; {_DEVICES_FILE_NAME} lists: {known}")
    if len(matches) > 1:
        raise ValueError(f"device '{mac}' is listed in more than one project")
    project, device = matches[0]

    given = [name for name, chosen in ((_FLAG_FIRMWARE, firmware),
                                       (_FLAG_PROVISION, provision),
                                       (_FLAG_SERIAL_FLASH, serial_flash),
                                       (_FLAG_FILE, file_name is not None),
                                       (_FLAG_COMMAND, command_name is not None)) if chosen]
    if len(given) != 1:
        raise ValueError(f"exactly one action is required: {_FLAG_FIRMWARE}, {_FLAG_PROVISION}, "
                         f"{_FLAG_SERIAL_FLASH}, {_FLAG_FILE} {_METAVAR_NAME} or "
                         f"{_FLAG_COMMAND} {_METAVAR_CMD} "
                         f"(got: {', '.join(given) if given else _NOTHING_LISTED})")

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
        known = ", ".join(f'"{f.name}"' for f in device.files) or _NOTHING_LISTED
        raise ValueError(f"device '{mac}' has no file entry named '{file_name}'; it accepts: {known}")
    for command in project.commands:
        if command.cmd == command_name:
            return ActionResult(project=project, device=device, command=command)
    known = ", ".join(c.cmd for c in project.commands) or _NOTHING_LISTED
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


def perform_rollout(device_manager: DeviceManager, projects: List[ProjectEntry],
                    config_manager: ConfigManager, mqtt_config: MQTTConfig,
                    dry_run: bool = False, assume_yes: bool = False) -> bool:
    """Prints the plan, gets it agreed to, runs it and reports. Shared by --rollout and the menu,
    so both show the same thing before sending anything and the same summary afterwards."""
    steps = device_manager.parse_rollout(projects)
    expected_hash = _expected_build_hash()
    planned = build_rollout_plan(steps, FleetStatus(mqtt_config).collect(), expected_hash)
    print(format_rollout_plan(planned, expected_hash))
    if dry_run:
        return True
    pending = sum(1 for p in planned if p.status is StepStatus.PENDING)
    if pending == 0:
        print("\nNothing to do.")
        return True
    if not assume_yes:
        answer = input(f"\nUpdate {pending} device(s) in this order? [y/N] ")
        if answer.strip().lower() not in ("y", "yes"):
            print("Cancelled.")
            return True
    ok = run_rollout(planned, config_manager, mqtt_config)
    print()
    print(format_rollout_summary(planned, expected_hash))
    return ok


def validate_args(args: argparse.Namespace) -> Optional[str]:
    """The combinations the parser itself cannot turn back, checked in one place so they can be.

    Returns the message to fail with, or None when the arguments go together. Each answer that
    stands alone refuses company, and each modifier names the action it belongs to: a run that
    typed one flag too many should say so rather than quietly pick one of them.
    """
    action_given = (bool(args.firmware) or bool(args.provision) or bool(args.serial_flash)
                    or args.file is not None or args.command is not None)
    if args.list and (args.device is not None or action_given or args.upload_port is not None
                      or args.rollout):
        return f"{_FLAG_LIST} {_TAKES_NO_OTHER_ARGUMENTS}"
    if args.status and (args.list or args.device is not None or action_given or args.upload_port is not None
                       or args.ota_timeout is not None or args.rollout):
        return f"{_FLAG_STATUS} {_TAKES_NO_OTHER_ARGUMENTS}"
    if args.rollout and (args.device is not None or action_given
                         or args.upload_port is not None or args.ota_timeout is not None):
        return f"{_FLAG_ROLLOUT} takes no arguments besides {_ROLLOUT_COMPANION_FLAGS}"
    if (args.dry_run or args.yes) and not args.rollout:
        return f"{_ROLLOUT_COMPANION_FLAGS} apply to {_FLAG_ROLLOUT} only"
    if args.device is None and action_given:
        return f"an action needs {_FLAG_DEVICE} {_METAVAR_MAC}"
    if args.upload_port is not None and (bool(args.firmware) or args.file is not None or args.command is not None):
        return f"{_FLAG_UPLOAD_PORT} applies to {_FLAG_PROVISION} and {_FLAG_SERIAL_FLASH} only"
    if args.ota_timeout is not None and not args.firmware:
        return f"{_FLAG_OTA_TIMEOUT} applies to {_FLAG_FIRMWARE} only"
    return None


def main():
    """Main entry point"""
    parser = build_arg_parser()
    args = parser.parse_args()
    complaint = validate_args(args)
    if complaint is not None:
        parser.error(complaint)

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

        if args.rollout:
            sys.exit(0 if perform_rollout(device_manager, projects, config_manager, mqtt_config,
                                          dry_run=bool(args.dry_run),
                                          assume_yes=bool(args.yes)) else 1)

        if args.device is not None:
            result = resolve_target(projects, str(args.device),
                                    firmware=bool(args.firmware),
                                    provision=bool(args.provision),
                                    serial_flash=bool(args.serial_flash),
                                    file_name=cast(Optional[str], args.file),
                                    command_name=cast(Optional[str], args.command))
        else:
            # Interactive target selection (project → device → action)
            def menu_rollout() -> None:
                perform_rollout(device_manager, projects, config_manager, mqtt_config)

            selected = select_target(projects, mqtt_config, menu_rollout)
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
