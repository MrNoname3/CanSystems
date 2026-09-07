"""Unit tests for scripts/board_console.py: the reset pulses themselves.

The pulses are the part worth pinning down - they say which control line each board's reset hangs
off, and a wrong line is silent rather than loud (the board simply does not restart). None of it
can be checked against hardware from a test, so what is checked here is the shape of each pulse
and that it reaches the port in order.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import board_console`
import board_console

# The one combination that pulls RESET/EN low on an ESP board, measured on a D1 mini.
ESP_RESET_LINES = (False, True)


class _RecordingPort:
    """Stands in for the serial port, keeping every state the lines pass through.

    The lines are written one at a time, so what a board sees is every intermediate state too, not
    only the ones a step asked for. `passed` holds all of them and `settled` only the held ones.
    """

    def __init__(self) -> None:
        self._dtr = True                                  # what opening the port leaves behind
        self._rts = True
        self.passed: list[tuple[bool, bool]] = []
        self.settled: list[tuple[bool, bool]] = []

    @property
    def dtr(self) -> bool:
        return self._dtr

    @dtr.setter
    def dtr(self, value: bool) -> None:
        self._dtr = value
        self.passed.append((self._dtr, self._rts))

    @property
    def rts(self) -> bool:
        return self._rts

    @rts.setter
    def rts(self, value: bool) -> None:
        self._rts = value
        self.passed.append((self._dtr, self._rts))

    def record_hold(self, seconds: float) -> None:
        """Passed to pulse() in place of sleep, so a snapshot lands once per step."""
        self.settled.append((self._dtr, self._rts))
        assert seconds >= 0.0


def _port_after(board: str) -> _RecordingPort:
    port = _RecordingPort()
    board_console.pulse(port, board_console.RESET_PULSES[board], sleep=port.record_hold)  # pyright: ignore[reportArgumentType]
    return port


def _walk(board: str) -> list[tuple[bool, bool]]:
    return _port_after(board).settled


def test_esp_pulse_holds_reset_low_once_and_releases_it() -> None:
    assert _walk("esp") == [(False, False), (False, True), (False, False)]


def test_esp_pulse_never_asserts_the_bootloader_line() -> None:
    # DTR drives GPIO0 there; asserting it would boot the ROM downloader instead of the firmware.
    assert all(not dtr for dtr, _rts in _walk("esp"))


def test_avr_pulse_puts_one_falling_edge_on_dtr() -> None:
    states = _walk("avr")
    # strict=False on purpose: pairing neighbours leaves the last state without a successor.
    edges = [(before[0], after[0]) for before, after in zip(states, states[1:], strict=False)
             if before[0] != after[0]]
    assert edges == [(False, True)]


def test_avr_pulse_leaves_rts_alone() -> None:
    # RTS is not connected on those boards; driving it would be pretending it does something.
    assert all(not rts for _dtr, rts in _walk("avr"))


def test_every_board_has_a_pulse_and_a_hold_for_each_step() -> None:
    for board, steps in board_console.RESET_PULSES.items():
        assert steps, f"{board} has no reset pulse"
        assert all(step.hold_s > 0.0 for step in steps), f"{board} has a step with no hold"


def test_every_pulse_starts_by_releasing_both_lines() -> None:
    # Opening a port leaves the lines somewhere of its own choosing, so a pulse that assumed a
    # starting state would sometimes produce no edge at all.
    for board in board_console.RESET_PULSES:
        assert _walk(board)[0] == (False, False), f"{board} does not release the lines first"


def test_the_esp_pulse_visits_the_reset_combination_exactly_once() -> None:
    # Including what the lines pass through between writes: a second visit would be a second reset.
    assert _port_after("esp").passed.count(ESP_RESET_LINES) == 1


def test_the_avr_pulse_never_visits_the_esp_reset_combination() -> None:
    # Naming the wrong board should be a quiet no-op, not a reset by accident on the way past.
    assert ESP_RESET_LINES not in _port_after("avr").passed


def test_only_the_boards_that_attach_quietly_may_skip_the_reset() -> None:
    # The list is what --no-reset is allowed for; a board missing from it resets on open anyway.
    assert board_console.QUIET_ATTACH_BOARDS <= set(board_console.RESET_PULSES)
    assert "avr" not in board_console.QUIET_ATTACH_BOARDS
