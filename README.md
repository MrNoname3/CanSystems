# CanSystems

Firmware monorepo for a distributed home-IoT system: ESP8266/ESP32 nodes talk to a Mosquitto
broker over MQTT/TLS (with optional Home Assistant auto-discovery), and an ESP32 gateway bridges
a 500 kbit/s CAN bus of ATmega328P devices onto MQTT. Everything — firmware, configuration files,
even the CAN devices' firmware — is updatable over the air through MQTT.

## Architecture

```
                 MQTT over TLS (8883)
  Server side  ─────────────────────────  Device side
┌──────────────────┐                  ┌─────────────────────────┐
│ Mosquitto broker │◄────────────────►│ ESP8266 nodes           │
│ Home Assistant   │                  │  • rad    (ENC28J60 LAN)│
│ ota/otaUpdate.py │                  │  • smoke  (Wi-Fi)       │
└──────────────────┘                  │  • thermo (Wi-Fi)       │
                                      ├─────────────────────────┤
                                      │ ESP32 CAN gateway       │
                                      │  (LAN8720 Ethernet)     │
                                      └───────────┬─────────────┘
                                          CAN bus │ 500 kbit/s, ext. ID
                                      ┌───────────┴─────────────┐
                                      │ ATmega328P devices      │
                                      │  • alert (LED+MP3+sens.)│
                                      │  • irrigation (pumps)   │
                                      └─────────────────────────┘
```

## Firmware targets (PlatformIO environments)

| Environment                | MCU        | Role |
|----------------------------|------------|------|
| `nanoatmega328_alert`      | ATmega328P | CAN alert node: WS2812 strip, DFPlayer MP3, Si7021 + LDR, pushbutton |
| `nanoatmega328_irrigation` | ATmega328P | CAN irrigation node: 4 pump channels, flow/current safety checks, moisture sensors |
| `project_esp8266_rad`      | ESP8266    | Geiger counter (CPM → µSv/h) + 433 MHz RF transceiver, ENC28J60 Ethernet |
| `project_esp8266_smoke`    | ESP8266    | Air quality node: ADS1115 + MQ-135 |
| `project_esp8266_thermo`   | ESP8266    | DS18B20 multi-probe thermometer (works with zero probes — handy as a test board) |
| `project_esp32_can`        | ESP32      | CAN↔MQTT gateway for the alert nodes, LAN8720 Ethernet |
| `native_test`              | host       | Native unit-test suite (custom runner + shims) |

The `nanoatmega328_bootloader_*` environments only burn the urboot bootloader and fuses
(see `notes.txt` for the avrdude commands and fuse values).

## MQTT scheme

- Device → server: `iot/dtos/<mac>/<subtopic>`; server → device: `iot/stod/<mac>/<subtopic>`.
- Every node publishes a retained `availability` topic (LWT) and a retained `info` topic
  (fw version = git commit count, git hash, dirty flag, reset reason).
- CAN sub-devices get their own sub-tree: `iot/dtos/<mac>/alert1/{availability,info,ota,button}`.
- Home Assistant MQTT discovery is opt-in via `"haDiscovery": true` in `server.json`;
  when disabled, the nodes actively retract their previously published entities.

## On-device configuration (LittleFS)

| File | Purpose |
|------|---------|
| `/config/server.json`  | Wi-Fi + MQTT credentials, server URL/port, `haDiscovery` toggle |
| `/config/mosq-ca.crt`  | CA certificate for TLS server validation (NTP sync runs first for X.509) |
| `/config/tube.json`    | Geiger tube type (rad node only) |

Per-device copies live in `ota/files/<mac>/`. `data/config` is a **git symlink** to
`ota/files/common` so the LittleFS image picks up the shared files — see Gotchas below.

## OTA and file transfer

**ESP firmware / files (MQTT):** `ota/otaUpdate.py` (interactive curses menu; configured by
`ota/config.yaml` + `ota/devices.yaml`) sends files as base64 pieces with per-piece ACK.
Firmware uploads carry a `binId` that the running firmware checks against its own PIO env, then
stream into the Updater and reboot; other files go to a temp file, are MD5-verified and renamed
into place (file names are allow-listed). The 100-byte piece size is deliberate — larger pieces
measured slower in practice.

**CAN device firmware (two-stage):** the ATmega firmware is first uploaded to the gateway's
LittleFS as `/canAlertFw.bin` (this also auto-triggers the CAN OTA), then streamed over the CAN
bus in 4-byte pieces with a CRC16. The ATmega stages it to its SPI flash (W25Q64); on reset the
urboot **dual-boot** bootloader programs the MCU from SPI flash. Result: `{"OTA":"[OK]"}` /
`{"OTA":"[ERR]"}` on the device's `ota` subtopic.

## Repository layout

| Path | Contents |
|------|----------|
| `src/main_*.cpp` | One entry point per environment (selected via `build_src_filter`) |
| `lib/`           | Feature libraries (task scheduler, MQTT/CAN stacks, drivers, OTA, …) |
| `test/`          | Native test suites + `test/_shims/` (Arduino/LittleFS/Update/PubSubClient fakes) |
| `ota/`           | Server-side OTA/file-transfer tool (Python venv: `paho-mqtt`, `pyyaml`, `tqdm`) |
| `scripts/`       | Build helpers (git version injection, ELF→BIN, library patching, size compare) and the release gate (`release_check.py`) |
| `bootloader/`    | Prebuilt urboot images for the ATmega nodes |
| `data/`          | LittleFS image source (`data/config` → symlink to `ota/files/common`) |
| `audio/`         | MP3 set for the alert node's DFPlayer SD card |
| `notes.txt`      | Fuse values, avrdude/bootloader build commands, misc. operational notes |

## Building, testing, flashing

```sh
pio run                                  # build all environments
pio run -e project_esp8266_thermo -t upload      # serial flash one target
pio run -t uploadfs -e <env>             # flash the LittleFS image (data/)
pio test -e native_test                  # native test suite (~30 s, 442 cases)
pio check                                # cppcheck + clang-tidy on all environments
```

The whole build is warning-clean under `-Wall -Wextra -Werror`; keep it that way.
Firmware version comes from the git commit count, so commit before flashing release builds
(the `dirty` flag is published in the info topic).

### Release gate

`scripts/release_check.py` chains the three commands above fail-fast: build all environments,
run the native test suite, then static analysis — where a **single defect of any severity**
(`--fail-on-defect low/medium/high`) fails the gate, unlike a bare `pio check`, which reports
SUCCESS even with findings. Step 0 checks the git tree with the same rule the firmware uses for
its `GIT_DIRTY` flag.

```sh
python3 scripts/release_check.py            # build + test + check; dirty tree is a warning
python3 scripts/release_check.py --strict   # release mode: a dirty tree fails immediately
```

Each command streams its output unchanged (through a pseudo-terminal, colors included), so a
human sees exactly what standalone runs would print. The run ends machine-friendly: a summary
table, the last 50 lines of the failed step (ANSI-stripped), and a final
`RELEASE CHECK: PASS|FAIL (step: <name>)` marker line — `tail` is enough to know everything.
Exit code is 0 only on a fully clean run (~5 minutes).

## Gotchas

- **`data/config` symlink:** requires `git config core.symlinks true`. If it ever turns into a
  19-byte regular file containing the path text, the LittleFS image silently loses the config
  files. File-sync tools can also break it (look for `*_Conflict` files).
- **Cross-project reflash:** a running firmware rejects an OTA image whose `binId` does not match
  its own environment, so converting a board to another project needs a one-time serial flash.
- **CAN IDs** are stored in EEPROM (CRC-protected). To provision a new node, build once with
  `NEW_CAN_ADDRESS` defined in `platformio.ini` (master ID is `MASTER_CAN_ADDRESS=10`), then
  remove it again.
