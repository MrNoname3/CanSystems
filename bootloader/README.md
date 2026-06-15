# Bootloader (urboot)

Pre-built [urboot](https://github.com/stefanrueger/urboot) bootloaders for the
ATmega328P nodes (`nanoatmega328_alert`, `nanoatmega328_irrigation`), compiled for
this project's custom wiring:

- ATmega328P @ 16 MHz, 115200 8N1 on **UART0** (RX = PD0, TX = PD1)
- activity **LED on PD4**, blink during I/O
- **dual boot** from an external SPI flash, chip-select on **PB0**
- EEPROM read/write + chip-erase support, 1 s watchdog timeout

## Files

| Hex | urboot | Protocol | Boot section | Burned by |
|-----|--------|----------|--------------|-----------|
| `urboot_771_…_stk500.hex` | u7.7.1 | STK500v1 (`URPROTOCOL=0`) | 1024 B (hfuse `0xD4`) | `nanoatmega328_bootloader_urboot771` |
| `urboot_800_…_hw.hex`     | u8.0   | urprotocol               | 512 B (hfuse `0xD6`)  | `nanoatmega328_bootloader_urboot800` |

STK500v1 was dropped from urboot u8.0, which is why the older 771 build still uses
it (and needs the larger 1024-byte section). The matching fuses live in
`platformio.ini`; burn a bootloader (via the ISP programmer) with e.g.:

```sh
pio run -e nanoatmega328_bootloader_urboot800 -t bootloader
```

## Rebuilding

`scripts/build_urboot.sh` reproduces these **byte-for-byte** in a container. It
works with **podman or docker**, and from both a host shell and the VS Code
Flatpak sandbox (the host engine is reached via `flatpak-spawn`). urboot is cloned
at a pinned tag *inside* the build image — nothing is vendored and no bind-mount is
used; the resulting `.hex` is streamed back out and normalised to LF. It clones from a
fork (`MrNoname3/urboot`) so the build does not depend on upstream staying available;
set `URBOOT_REPO=https://github.com/stefanrueger/urboot` to build from upstream instead.

```sh
scripts/build_urboot.sh                                # all variants -> bootloader/
scripts/build_urboot.sh 800                            # just one
URBOOT_OUT_DIR=/tmp/x scripts/build_urboot.sh 800      # write elsewhere (e.g. to diff)
```

The variant → tag mapping and any extra make options live in `variant_ref()` /
`variant_extra()` near the top of the script; `801` (urboot u8.0.1) is wired up for
future use. When it finishes, the script offers to delete the build images to
reclaim space.
