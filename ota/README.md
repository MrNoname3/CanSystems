# OTA / file-transfer tool

Interactive Python tool that pushes firmware and configuration files to the devices over
MQTT. It connects to the same broker the devices use, lets you pick a target (project →
device → action) in a terminal UI, then streams the chosen file as base64 pieces with a
per-piece ACK (see the repo README for the wire protocol).

It reads two files from this directory:

- **`config.yaml`** — broker connection + credentials (you create this; it is git-ignored).
- **`devices.yaml`** — the device list and per-device file mappings (tracked; see the existing
  file for the structure).

The CA cert referenced by `cafile` (and uploaded to the devices as `mosq-ca.crt`) is
per-deployment and git-ignored. For a broker behind a public CA it is just that CA's roots —
e.g. for Let's Encrypt the ISRG Root X1 + X2 bundle; a self-signed broker cert is not needed.

## Setup

The tool's runtime deps (`paho-mqtt`, `pyyaml`, `tqdm`) are in `requirements.txt`. They are
also pulled in by the project-root `requirements-dev.txt`, so if you already created the
root `.venv` for the release gate you have them. For a runtime-only install:

```sh
python -m venv .venv
.venv/bin/pip install -r ota/requirements.txt
```

## `config.yaml`

Create `ota/config.yaml` next to the script. It holds the broker credentials, so it is
git-ignored — never commit it. Full example (TLS + authentication, the typical setup):

```yaml
# MQTT broker connection for the OTA tool.
protocol: mqtt           # "mqtt" or "ws" (WebSocket)
host: broker.example.com # broker hostname or IP (required)
port: 8883               # optional; defaults from protocol+TLS if omitted (see below)
client_id: OtaUpdater    # optional; a random one is generated if empty

# Authentication (optional — omit both for an anonymous broker).
username: ota
password: change-me

# TLS (recommended). When tls_enabled is true, point cafile at the broker's CA cert.
# Unlike the paths in devices.yaml, cafile is resolved relative to your *current directory*,
# so an absolute path is safest (e.g. /home/you/CanSystems/ota/files/common/mosq-ca.crt).
tls_enabled: true
cafile: ota/files/common/mosq-ca.crt   # works when launched from the repo root

# basepath: /            # only used with the "ws" protocol
```

Minimal anonymous, non-TLS example:

```yaml
host: 192.168.1.10
tls_enabled: false
```

Default port when `port` is omitted (or `0`):

| protocol | TLS off | TLS on |
|----------|---------|--------|
| `mqtt`   | 1883    | 8883   |
| `ws`     | 80      | 443    |

## Device config files (`ota/files/`)

These are the files the tool uploads to a device's `/config/` (per `devices.yaml`). They are
per-deployment and git-ignored (`server.json`, `*.crt`), so a fresh clone won't have them —
create them as needed. Paths are relative to `ota/`, and `ota/files/common/` is also the target
of the `data/config` symlink, so files placed there are picked up by the LittleFS image too.

**`server.json`** — the WiFi + MQTT credentials the firmware reads from `/config/server.json`
(JSON; `//` comments are allowed). All keys live in this one file:

```json
{
  "ssid": "MyWiFi",
  "password": "wifi-secret",
  "mqttUserName": "device01",
  "mqttPassword": "mqtt-secret",
  "mqttServerUrl": "broker.example.com",
  "mqttServerPort": 8883,
  "haDiscovery": true        // optional, default false: Home Assistant auto-discovery
}
```

**`tube.json`** — radiation node only; selects the Geiger tube type:

```json
{ "tube": 1 }   // 1 = J305, 2 = M4011
```

**`mosq-ca.crt`** — the broker's CA certificate (see the note above).

## Running

```sh
.venv/bin/python ota/otaUpdate.py
```

The file paths in `devices.yaml` resolve relative to `ota/` (and the firmware binaries
relative to the repo root), so the tool works from any directory — only `cafile` above
follows your current directory. The examples here assume you launch it from the repo root.

Pick a project, then a device, then an action (firmware or a config file). Firmware actions
expect the build output at `.pio/build/<env>/firmware.bin`, so build the target first
(`pio run -e <env>`).
