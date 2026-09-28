"""Unit tests for scripts/analysis_check.py: the clang-tidy that ran, its version, the defects it
counts and the parse errors it fails on.

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


def _output_with_error(environment: str, path: str) -> str:
    return VERBOSE_OUTPUT.replace(
        f"Checking {environment} > clangtidy (platform: example; board: example)\n",
        f"Checking {environment} > clangtidy (platform: example; board: example)\n"
        f"{path}:4:10: error: 'connectivity.hpp' file not found [clang-diagnostic-error]\n")


PROJECT_HEADER = str(analysis_check.PROJECT_DIR / "lib" / "mqttCommon" / "src" / "mqttCommon.hpp")


def _run_main(monkeypatch: object, output: str) -> int:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio: (0, output))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    return analysis_check.main()


def test_a_clang_error_in_project_code_fails_the_gate(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "EXPECTED_ERRORS", {})   # type: ignore[attr-defined]
    assert _run_main(monkeypatch, _output_with_error("check_avr", PROJECT_HEADER)) == 1


def test_an_expected_clang_error_passes(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "EXPECTED_ERRORS",   # type: ignore[attr-defined]
                        {"check_avr": (("lib/mqttCommon/*", "ESP-only"),)})
    assert _run_main(monkeypatch, _output_with_error("check_avr", PROJECT_HEADER)) == 0


def test_an_expected_error_that_no_longer_occurs_is_named() -> None:
    per_environment = analysis_check.diagnostics_per_environment(VERBOSE_OUTPUT)
    _, unused = analysis_check.check_errors(per_environment)
    assert "check_avr: lib/configHandler/*" in unused


def test_an_error_expected_in_another_environment_still_fails(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "EXPECTED_ERRORS",   # type: ignore[attr-defined]
                        {"check_esp32": (("lib/mqttCommon/*", "ESP-only"),)})
    assert _run_main(monkeypatch, _output_with_error("check_avr", PROJECT_HEADER)) == 1


def test_clang_errors_outside_the_project_code_pass(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "EXPECTED_ERRORS", {})   # type: ignore[attr-defined]
    framework = "/home/user/.platformio/packages/framework-arduinoespressif32/cores/esp32/pgmspace.h"
    library = str(analysis_check.PROJECT_DIR / ".pio" / "libdeps" / "check_avr" / "NeoPixelBus" / "src" / "NeoPixelBus.h")
    assert _run_main(monkeypatch, _output_with_error("check_avr", framework)) == 0
    assert _run_main(monkeypatch, _output_with_error("check_avr", library)) == 0


def _output_with_defect(path: str) -> str:
    return VERBOSE_OUTPUT + f"{path}:142: [medium:warning] Out of bound access  [clang-analyzer-security.ArrayBound]\n"


def test_a_defect_in_project_code_fails_the_gate(monkeypatch: object) -> None:
    assert _run_main(monkeypatch, _output_with_defect("lib/canCommissioner/src/canCommissioner.cpp")) == 1


def test_defects_in_library_and_framework_code_pass(monkeypatch: object) -> None:
    library = ".pio/libdeps/check_esp32/ArduinoJson/src/ArduinoJson/Numbers/parseNumber.hpp"
    framework = "/home/user/.platformio/packages/framework-arduinoespressif32-libs/esp32/include/sdkconfig.h"
    assert _run_main(monkeypatch, _output_with_defect(library)) == 0
    assert _run_main(monkeypatch, _output_with_defect(framework)) == 0


def test_a_failing_tool_still_fails_the_gate(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio: (1, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main() == 1
