# OTA / file-transfer tool

Interactive Python tool that pushes firmware and configuration files to the devices over
MQTT. It connects to the same broker the devices use, lets you pick a target (project →
device → action) in a terminal UI, then streams the chosen file as base64 pieces with a
per-piece ACK (see the repo README for the wire protocol).

It reads two files from this directory:

- **`secrets.yaml`** — every per-deployment secret in one git-ignored file: the tool's broker
  connection and the devices' `server.json` fields (you create this; template below). When
  cloning the repo on another machine, this is the single file to carry over manually
  (plus `mosq-ca.crt`, see below).
- **`devices.yaml`** — the device list, per-device file mappings and non-secret device config
  (tracked; see the existing file for the structure).

The CA cert (uploaded to the devices as `mosq-ca.crt`) is per-deployment and git-ignored.
For a broker behind a public CA it is just that CA's roots — e.g. for Let's Encrypt the
ISRG Root X1 + X2 bundle; a self-signed broker cert is not needed.

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

**`mosq-ca.crt`** — the broker's CA certificate (see the note above), expected at
`ota/mosq-ca.crt` (git-ignored).

## Running

```sh
.venv/bin/python ota/otaUpdate.py
```

All paths (`devices.yaml` entries, the broker `cafile`, firmware binaries) resolve relative
to `ota/` or the repo root, so the tool works from any directory.

Pick a project, then a device, then an action. Firmware actions expect the build output at
`.pio/build/<env>/firmware.bin`, so build the target first (`pio run -e <env>`).

### Initial provisioning (USB)

A brand-new (or wiped) device has no config to connect with, so the OTA path can't reach it.
The **Initial provisioning** action flashes the first LittleFS image over serial instead:
it clears `data/`, materializes the device's `/config/*` files there (rendered
`server.json`, the CA cert, `tube.json`, ...), runs `pio run -e <env> -t uploadfs`, then
clears `data/` again. The device gets **its own** credentials from the very first flash.
`data/` is transient and git-ignored — don't keep anything there. Flash the firmware itself
separately (`pio run -e <env> -t upload`).
