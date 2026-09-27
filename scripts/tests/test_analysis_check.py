"""Unit tests for scripts/analysis_check.py: finding the clang-tidy that ran and its version.

Running `pio check` itself is the gate's job; these cover reading its output and the version the
binary reports.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import analysis_check`
import analysis_check

BINARY = "/home/user/.platformio/packages/tool-clangtidy/clang-tidy"
VERBOSE_OUTPUT = "".join(
    f"Checking {environment} > clangtidy (platform: example; board: example)\n"
    f"{BINARY} --quiet --config-file=.clang-tidy lib/crc16/src/crc16.cpp -- -DPLATFORMIO=60200\n"
    f"lib/crc16/src/crc16.cpp:3:1: warning: something [readability-something]\n"
    for environment in analysis_check.ENVIRONMENTS)

VERSION_OUTPUT = "LLVM (http://llvm.org/):\n  LLVM version 21.1.0\n  Optimized build.\n"


def _reports_expected_version(binary: str) -> str:
    return analysis_check.CLANG_TIDY_VERSION


def test_the_binary_is_read_from_the_echoed_command() -> None:
    assert analysis_check.clang_tidy_binary(VERBOSE_OUTPUT) == BINARY


def test_no_binary_when_clang_tidy_never_ran() -> None:
    assert analysis_check.clang_tidy_binary("Checking check_avr > clangtidy (...)\nNo defects found\n") is None


def test_the_llvm_version_is_parsed() -> None:
    assert analysis_check.parse_version(VERSION_OUTPUT) == "21.1.0"


def test_unrecognised_version_output_gives_none() -> None:
    assert analysis_check.parse_version("clang-tidy: error while loading shared libraries\n") is None


def test_a_different_version_fails_the_gate(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio: (0, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", lambda binary: "22.1.0")   # type: ignore[attr-defined]
    assert analysis_check.main() == 1


def test_the_expected_version_passes(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio: (0, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main() == 0


def test_output_without_the_command_fails_the_gate(monkeypatch: object) -> None:
    without_command = "\n".join(line for line in VERBOSE_OUTPUT.splitlines() if BINARY not in line)
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio: (0, without_command))   # type: ignore[attr-defined]
    assert analysis_check.main() == 1
