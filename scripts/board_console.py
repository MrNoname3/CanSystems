#!/usr/bin/env python3
"""Reset a board over its USB-serial adapter and print what it says afterwards.

Every board here resets from the adapter's control lines, but not from the same ones, and the
circuit doing it sits on the board rather than in the USB-serial chip - the same CH340 carries an
ESP8266 D1 mini and an ATmega328P Nano. Nothing in the USB descriptors says which is which, so the
wiring is named rather than guessed:

  esp  ESP8266 / ESP32 boards. Two transistors across DTR and RTS, wired so that only a difference
       between the lines pulls anything low. Measured on a D1 mini: RESET/EN goes low for exactly
       one combination, RTS asserted while DTR is not, and no other. DTR is left released
       throughout, so the board comes up in its application instead of the ROM downloader. Opening
       the port raises both lines together, which is why attaching to one of these does not reset
       it - see --no-reset.
  avr  ATmega328P boards. DTR alone, coupled to RESET through a capacitor; RTS is not connected.
       Asserting DTR is the reset, and opening the port asserts it, so there is no way to watch one
       of these without restarting it first. --no-reset is refused for that reason.

The pulse and the reading share one open port on purpose: driving the lines from a second process
while another one holds the port was observed to do nothing at all.

What a board reports about the reset afterwards differs too, and none of it is decoded here - the
CAN nodes' bitmask is in README.md, and what an ESP32 reset does to the retained bytes is in
lib/rtcStore/src/rtcStore.hpp.

Usage:
  python scripts/board_console.py --board esp                 # reset, then print 10 s of output
  python scripts/board_console.py --board avr --listen 20     # reset, then print 20 s
  python scripts/board_console.py --board esp --no-reset      # attach without resetting
  python scripts/board_console.py --board avr --port /dev/ttyUSB1
"""

import argparse
import sys
import time
from collections.abc import Callable, Iterable
from dataclasses import dataclass

import serial
from serial.tools import list_ports

BAUD = 115200          # every environment's monitor_speed, and what the firmwares open Serial with
DEFAULT_LISTEN_S = 10.0


@dataclass(frozen=True)
class LineStep:
    """One step of a reset pulse: where the two control lines go, and how long they stay there."""

    dtr: bool
    rts: bool
    hold_s: float


# The knowledge this script exists to carry. Each sequence is read as settled states, in order.
RESET_PULSES: dict[str, tuple[LineStep, ...]] = {
    # Both lines alike leaves RESET and GPIO0 released; only RTS asserted pulls RESET low; back to
    # alike releases it. DTR never goes true, so GPIO0 stays high and the application boots.
    "esp": (
        LineStep(dtr=False, rts=False, hold_s=0.05),
        LineStep(dtr=False, rts=True, hold_s=0.15),
        LineStep(dtr=False, rts=False, hold_s=0.05),
    ),
    # Releasing DTR lets the coupling capacitor charge; asserting it puts a falling edge on RESET.
    # It is left asserted, which is the state an open serial monitor holds a board in anyway.
    "avr": (
        LineStep(dtr=False, rts=False, hold_s=0.20),
        LineStep(dtr=True, rts=False, hold_s=0.05),
    ),
}

# Attaching to these boards raises both lines together and leaves them alike, so nothing resets.
QUIET_ATTACH_BOARDS = frozenset({"esp"})


def pulse(port: serial.Serial, steps: Iterable[LineStep],
          sleep: Callable[[float], None] = time.sleep) -> None:
    """Walks the control lines through `steps`, holding each state for its own time.

    The two lines can only be written one at a time, so a step passes through a state nobody asked
    for on its way. Releasing RTS first when the step wants it released is what keeps that passing
    state from being the one combination that resets an ESP board - otherwise every step that let
    both lines go would reset one on the way past.
    """
    for step in steps:
        if step.rts:
            port.dtr = step.dtr
            port.rts = step.rts
        else:
            port.rts = step.rts
            port.dtr = step.dtr
        sleep(step.hold_s)


def read_for(port: serial.Serial, seconds: float) -> bytes:
    """Collects whatever arrives over `seconds`, however little that turns out to be."""
    collected = bytearray()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        collected += port.read(4096)
    return bytes(collected)


def pick_port(requested: str | None) -> str:
    """The port to talk to: the one asked for, or the single USB adapter attached."""
    if requested is not None:
        return requested
    attached = [p for p in list_ports.comports() if p.vid is not None]
    if not attached:
        sys.exit("no USB serial adapter is attached; name one with --port")
    if len(attached) > 1:
        listing = "\n".join(f"  {p.device}  {p.description}  {p.hwid}" for p in attached)
        sys.exit(f"several USB serial adapters are attached; name one with --port:\n{listing}")
    return attached[0].device


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reset a board over its USB-serial adapter and print what it says afterwards.",
        epilog="The board's wiring has to be named: the USB adapter does not reveal what it is "
               "attached to.")
    parser.add_argument("--board", required=True, choices=sorted(RESET_PULSES),
                        help="which reset wiring the board has")
    parser.add_argument("--port", metavar="DEV",
                        help="serial port; defaults to the single attached USB adapter")
    parser.add_argument("--listen", type=float, default=DEFAULT_LISTEN_S, metavar="SECONDS",
                        help=f"how long to read after the reset (default {DEFAULT_LISTEN_S:g})")
    parser.add_argument("--no-reset", action="store_true",
                        help="attach and read without resetting; only where attaching is quiet")
    args = parser.parse_args()

    board: str = args.board
    if args.no_reset and board not in QUIET_ATTACH_BOARDS:
        parser.error(f"--no-reset does not apply to a '{board}' board: opening the port asserts "
                     f"DTR, which is that board's reset")

    port_name = pick_port(args.port)
    print(f"{port_name} @ {BAUD}, {board} wiring, "
          f"{'listening only' if args.no_reset else 'reset'} for {args.listen:g}s", file=sys.stderr)

    with serial.Serial(port_name, BAUD, timeout=0.2) as port:
        if not args.no_reset:
            port.reset_input_buffer()
            pulse(port, RESET_PULSES[board])
        output = read_for(port, args.listen)

    sys.stdout.write(output.decode("utf-8", "replace"))
    sys.stdout.flush()
    if not output and not args.no_reset:
        # A board that was reset always says something. Silence means the pulse reached nothing -
        # most often the other wiring, which drives lines this board does not have connected.
        print(f"\nnothing arrived after the reset; is this really a '{board}' board?",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
