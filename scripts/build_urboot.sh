#!/usr/bin/env sh
# Build the urboot bootloader reproducibly in a container — podman OR docker, from
# inside the VS Code Flatpak sandbox OR a plain host shell.
#
# It clones urboot at a PINNED tag INSIDE the image (no host clone, no bind-mount),
# runs `make` with THIS project's wiring (UART0 on PD0/PD1, LED on PD4, dual boot
# from an SPI flash with CS on PB0), and writes <name>.hex into bootloader/.
#
# Usage:
#   scripts/build_urboot.sh [771|800|801 ...]    # default: all three
#   URBOOT_OUT_DIR=/tmp/x scripts/build_urboot.sh 800   # write elsewhere (e.g. to diff)
#
# Variants map to urboot releases. 771 (tag u7.7.1) is built with the STK500v1
# protocol (URPROTOCOL=0, dropped upstream from u8.0 onwards) and so is the larger,
# 1024-byte boot-section build; 800 (tag u8.0) and 801 (tag u8.0.1) use urboot's own
# urprotocol and fit the 512-byte section. The matching fuses live in platformio.ini
# (nanoatmega328_bootloader_urboot771 / ...800).
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOCKERFILE="$PROJECT_DIR/bootloader/urboot.Dockerfile"
OUT_DIR="${URBOOT_OUT_DIR:-$PROJECT_DIR/bootloader}"
# urboot source repo; defaults to a fork (insurance against upstream changes/deletion).
# Override to cross-check against upstream: URBOOT_REPO=https://github.com/stefanrueger/urboot
URBOOT_REPO="${URBOOT_REPO:-https://github.com/MrNoname3/urboot}"

# --- host access (same idea as the ADB tooling's _common.sh) ----------------
# Inside the Flatpak sandbox the container engine lives on the host, reached via
# `flatpak-spawn --host`; on a normal host shell, commands run directly. Every
# engine call and the stdin/stdout streams route through hostrun() so the rest of
# the script never cares which side it runs on.
if [ -f /.flatpak-info ]; then
    hostrun() { flatpak-spawn --host "$@"; }
else
    hostrun() { "$@"; }
fi

# --- container engine: prefer podman, fall back to docker (checked on host) --
if hostrun sh -c 'command -v podman >/dev/null 2>&1'; then
    ENGINE=podman
elif hostrun sh -c 'command -v docker >/dev/null 2>&1'; then
    ENGINE=docker
else
    echo "error: neither podman nor docker found on the host" >&2
    exit 1
fi

# --- shared bootloader config (this project's custom wiring) -----------------
COMMON_OPTS="MCU=atmega328p WDTO=1S F_CPU=16000000L BAUD_RATE=115200 UARTNUM=0 RX=AtmelPD0 TX=AtmelPD1 VBL=0 EEPROM=1 CHIP_ERASE=1 LED=AtmelPD4 SFMCS=AtmelPB0 DUAL=1 BLINK=1 AUTOFRILLS=0,6,8..10,5,4,3,2"

# --- per-variant: urboot tag, extra make options, exact output base name -----
variant_ref()   { case "$1" in 771) echo u7.7.1 ;; 800) echo u8.0 ;; 801) echo u8.0.1 ;; esac; }
variant_extra() { case "$1" in 771) echo "URPROTOCOL=0" ;; *) echo "" ;; esac; }
variant_name()  {
    case "$1" in
        771) echo "urboot_771_m328p_1s_x16m0_115k2_uart0_rxd0_txd1_led+d4_csb0_dual_ee_ce_hw_stk500" ;;
        800) echo "urboot_800_m328p_1s_x16m0_115k2_uart0_rxd0_txd1_led+d4_csb0_dual_ee_ce_hw" ;;
        801) echo "urboot_801_m328p_1s_x16m0_115k2_uart0_rxd0_txd1_led+d4_csb0_dual_ee_ce_hw" ;;
    esac
}

build_one() {
    variant=$1
    ref=$(variant_ref "$variant")
    name=$(variant_name "$variant")
    if [ -z "$ref" ] || [ -z "$name" ]; then
        echo "error: unknown variant '$variant' (use 771|800|801)" >&2
        return 1
    fi
    opts="$COMMON_OPTS $(variant_extra "$variant") NAME=$name"
    image="urboot-builder:$ref"

    echo ">>> [$variant] build image from $URBOOT_REPO @ $ref"
    hostrun "$ENGINE" build --platform linux/amd64 \
        --build-arg "URBOOT_REF=$ref" --build-arg "URBOOT_REPO=$URBOOT_REPO" \
        -t "$image" - < "$DOCKERFILE"

    echo ">>> [$variant] make $opts"
    # Override the `make` entrypoint with a shell so we can run make (log to stderr)
    # and then cat the .hex to stdout; flatpak-spawn relays stdout back to us, so the
    # hex lands in $tmp on this side with no host-path translation. --rm cleans up.
    tmp=$(mktemp)
    if hostrun "$ENGINE" run --rm --platform linux/amd64 --entrypoint /bin/sh "$image" \
            -c "make $opts >&2 && cat $name.hex" > "$tmp" && [ -s "$tmp" ]; then
        mkdir -p "$OUT_DIR"
        # urboot emits Intel HEX with CRLF line endings (the format's convention); the
        # in-repo reference hexes are stored LF-only, so normalise to match byte-for-byte.
        tr -d '\r' < "$tmp" > "$OUT_DIR/$name.hex"
        rm -f "$tmp"
        echo ">>> [$variant] wrote $OUT_DIR/$name.hex"
    else
        rm -f "$tmp"
        echo "error: [$variant] build failed" >&2
        return 1
    fi
}

# --- offer to reclaim the build images (interactive only, default yes) ------
prune_images() {
    [ -t 0 ] && [ -t 1 ] || return 0          # only ask when run interactively
    set -- $(hostrun "$ENGINE" images --filter reference='urboot-builder' \
                 --format '{{.Repository}}:{{.Tag}}' 2>/dev/null | sort -u)
    [ "$#" -gt 0 ] || return 0
    printf 'Remove the %d urboot-builder build image(s) to reclaim space? [Y/n] ' "$#"
    read -r answer || answer=""
    case "${answer:-Y}" in
        [Nn]*) echo "Kept build images." ;;
        *)     hostrun "$ENGINE" rmi -f "$@" >/dev/null 2>&1 && echo "Removed build images." ;;
    esac
}

for v in ${*:-771 800 801}; do
    build_one "$v"
done

prune_images
