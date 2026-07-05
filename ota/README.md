# OTA / file-transfer tool

Interactive Python tool that pushes firmware and configuration files to the devices over
MQTT. It connects to the same broker the devices use, lets you pick a target (project →
device → action) in a terminal UI, then streams the chosen file as base64 pieces with a
per-piece ACK (see the repo README for the wire protocol).

It reads two files from this directory:

- **`secrets.yaml`** — every per-deployment secret in one git-ignored file: the tool's broker
  connection and the devices' `server.json` fields (you create this; template below). When
  cloning the repo on another machine, this is the **only** file to carry over manually.
- **`devices.yaml`** — the device list, per-device file mappings and non-secret device config
  (tracked; see the existing file for the structure).

The CA bundle uploaded to the devices as `mosq-ca.crt` is git-ignored but needs no manual
transfer: when `ota/mosq-ca.crt` is missing, the tool generates it from the **system trust
store** (by default the Let's Encrypt ISRG Root X1 + X2 roots; override the subject CNs with
the `ca_roots` list in `secrets.yaml`). A hand-placed file is never overwritten, so a
self-signed CA can simply be dropped there.

**Identity preflight.** Before every action that ships connection config to a device —
the CA certificate and server.json uploads over MQTT and the USB Initial provisioning —
the tool first connects to the broker **as that device would**: same host/port, the
device's own MQTT credentials from `secrets.yaml`, TLS validated with the exact CA bundle
about to be shipped. It connects as `verify_<mac>`, so broker logs show whose identity was
tested while the device's live session (whose client id embeds the project name too) is
never taken over. If the broker rejects it, nothing is uploaded — a typo in `secrets.yaml`
or a bad bundle cannot lock a deployed device out.

## Setup

The tool's runtime deps (`paho-mqtt`, `pyyaml`, `tqdm`) are in `requirements.txt`. They are
also pulled in by the project-root `requirements-dev.txt`, so if you already created the
root `.venv` for the release gate you have them. For a runtime-only install:

```sh
python -m venv .venv
.venv/bin/pip install -r ota/requirements.txt
```

## `secrets.yaml`

Create `ota/secrets.yaml` next to the script. It holds every per-deployment secret, so it is
git-ignored — never commit it. Full example (TLS + authentication, the typical setup):

```yaml
# MQTT broker connection for the OTA tool itself.
broker:
  protocol: mqtt           # "mqtt" or "ws" (WebSocket)
  host: broker.example.com # broker hostname or IP (required)
  port: 8883               # optional; defaults from protocol+TLS if omitted (see below)
  client_id: OtaUpdater    # optional; a random one is generated if empty

  # Authentication (optional — omit both for an anonymous broker).
  username: ota
  password: change-me

  # TLS (recommended). When tls_enabled is true, point cafile at the broker's CA cert,
  # or leave it null to use the system CA store. A relative path resolves against ota/.
  tls_enabled: true
  cafile: mosq-ca.crt

  # basepath: /            # only used with the "ws" protocol

# pio: /custom/path/to/pio # optional; provisioning default: ~/.platformio/penv/bin/pio, then PATH

# ca_roots:                # optional; system-store subject CNs for the generated mosq-ca.crt
#   - ISRG Root X1         # (these two Let's Encrypt roots are the default)
#   - ISRG Root X2

# server.json fields shared by every device; a device entry below can override them.
server_defaults:
  mqttServerUrl: broker.example.com
  mqttServerPort: 8883

# Per-device server.json secrets, keyed by MAC. mqttUserName/mqttPassword are required;
# add ssid/password for Wi-Fi devices. Non-secret fields (haDiscovery) live in devices.yaml.
devices:
  a1b2c3d4e5f6:
    mqttUserName: device01
    mqttPassword: mqtt-secret
    ssid: MyWiFi             # Wi-Fi devices only
    password: wifi-secret    # Wi-Fi devices only
```

Minimal anonymous, non-TLS example (no devices yet):

```yaml
broker:
  host: 192.168.1.10
  tls_enabled: false
```

Default broker port when `port` is omitted (or `0`):

| protocol | TLS off | TLS on |
|----------|---------|--------|
| `mqtt`   | 1883    | 8883   |
| `ws`     | 80      | 443    |

## Device config files

The files the tool puts on a device's `/config/` (per `devices.yaml`):

**`server.json`** — the WiFi + MQTT credentials the firmware reads from `/config/server.json`.
It is **not stored anywhere** — the tool renders it on the fly from `secrets.yaml`
(`server_defaults` merged with the device's `devices:` entry) plus the device's non-secret
`server_config` mapping in `devices.yaml` (currently just `haDiscovery`, the optional
Home Assistant auto-discovery toggle, default false). A `devices.yaml` file entry selects
the rendered content with `render: server_json` instead of a `local_path`.

**`tube.json`** — radiation node only; selects the Geiger tube type (1 = J305, 2 = M4011).
Non-secret and tiny, so it lives **inline** in the device's `devices.yaml` file entry as a
`content:` mapping, serialized to compact JSON at send time. Any small JSON config can be
inlined the same way instead of pointing `local_path` at a file.

**`mosq-ca.crt`** — the broker's CA bundle at `ota/mosq-ca.crt` (git-ignored); generated
from the system trust store when missing (see the note above).

## Running

```sh
.venv/bin/python ota/otaUpdate.py
```

All paths (`devices.yaml` entries, the broker `cafile`, firmware binaries) resolve relative
to `ota/` or the repo root, so the tool works from any directory.

Pick a project, then a device, then an action. Firmware actions expect the build output at
`.pio/build/<env>/firmware.bin`, so build the target first (`pio run -e <env>`).

### Bench setup of a fresh device (USB)

A brand-new (or wiped) device has no config to connect with, so the OTA path can't reach it.
Two menu actions cover the full bench setup — running both leaves a ready device:

- **Initial firmware flash** — builds the project's firmware (incrementally) and flashes it
  over serial (`pio run -e <env> -t upload`).
- **Initial provisioning** — flashes the first LittleFS image over serial: it clears `data/`,
  materializes the device's `/config/*` files there (rendered `server.json`, the CA cert,
  `tube.json`, ...), runs `pio run -e <env> -t uploadfs`, then clears `data/` again. The
  device gets **its own** credentials from the very first flash. `data/` is transient and
  git-ignored — don't keep anything there.
