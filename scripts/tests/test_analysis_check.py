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
    f"TIDY_COMPLETE {environment} 0 1\n"
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
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", lambda binary: "22.1.0")   # type: ignore[attr-defined]
    assert analysis_check.main() == 1


def test_the_expected_version_passes(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main() == 0


def test_output_without_the_command_fails_the_gate(monkeypatch: object) -> None:
    without_command = "\n".join(line for line in VERBOSE_OUTPUT.splitlines() if BINARY not in line)
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, without_command))   # type: ignore[attr-defined]
    assert analysis_check.main() == 1


def _output_with_error(environment: str, path: str) -> str:
    return VERBOSE_OUTPUT.replace(
        f"Checking {environment} > clangtidy (platform: example; board: example)\n",
        f"Checking {environment} > clangtidy (platform: example; board: example)\n"
        f"{path}:4:10: error: 'connectivity.hpp' file not found [clang-diagnostic-error]\n")


PROJECT_HEADER = str(analysis_check.PROJECT_DIR / "lib" / "mqttCommon" / "src" / "mqttCommon.hpp")


def _run_main(monkeypatch: object, output: str) -> int:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, output))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    return analysis_check.main()


def test_a_clang_error_in_project_code_fails_the_gate(monkeypatch: object) -> None:
    assert _run_main(monkeypatch, _output_with_error("check_avr", PROJECT_HEADER)) == 1


def test_clang_errors_outside_the_project_code_pass(monkeypatch: object) -> None:
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
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (1, VERBOSE_OUTPUT))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main() == 1


def _output_for(environment: str) -> str:
    """What `pio check --verbose -e <environment>` prints: VERBOSE_OUTPUT's part for that one."""
    return (f"Checking {environment} > clangtidy (platform: example; board: example)\n"
            f"{BINARY} --quiet --config-file=.clang-tidy lib/crc16/src/crc16.cpp -- -DPLATFORMIO=60200\n"
            f"lib/crc16/src/crc16.cpp:3:1: warning: something [readability-something]\n"
            f"TIDY_COMPLETE {environment} 0 1\n")


def test_env_runs_only_the_named_environment(monkeypatch: object) -> None:
    ran: list[list[str]] = []

    def run_check(pio: str, environments: list[str]) -> tuple[int, str]:
        ran.append(list(environments))
        return 0, _output_for("check_avr")

    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", run_check)   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main(["--env", "check_avr"]) == 0
    assert ran == [["check_avr"]]


def test_an_environment_left_out_is_not_reported_silent(monkeypatch: object, capsys: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, _output_for("check_esp8266")))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main(["--env", "check_esp8266"]) == 0
    printed: str = capsys.readouterr().out   # type: ignore[attr-defined]
    assert "check_avr" not in printed and "check_esp32" not in printed


def test_a_named_environment_that_analysed_nothing_still_fails(monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "run_check", lambda pio, environments: (0, _output_for("check_avr")))   # type: ignore[attr-defined]
    monkeypatch.setattr(analysis_check, "clang_tidy_version", _reports_expected_version)   # type: ignore[attr-defined]
    assert analysis_check.main(["--env", "check_avr", "check_esp32"]) == 1


def test_an_unknown_environment_is_refused() -> None:
    try:
        analysis_check.main(["--env", "project_esp32_can"])
    except SystemExit as stop:
        assert stop.code == 2
    else:
        raise AssertionError("an environment outside ENVIRONMENTS was accepted")


def test_partial_output_after_a_diagnostic_fails(monkeypatch: object) -> None:
    assert _run_main(monkeypatch, "\n".join(line for line in VERBOSE_OUTPUT.splitlines()
                                           if not line.startswith("TIDY_COMPLETE "))) == 1


def test_a_clean_completed_analysis_needs_no_diagnostics(monkeypatch: object) -> None:
    output = "".join(
        f"Checking {env} > clangtidy (platform: example)\n{BINARY} --quiet input.cpp --\n"
        f"TIDY_COMPLETE {env} 0 1\n" for env in analysis_check.ENVIRONMENTS)
    assert _run_main(monkeypatch, output) == 0


def test_a_crash_after_diagnostics_fails(monkeypatch: object) -> None:
    assert _run_main(monkeypatch, VERBOSE_OUTPUT.replace("TIDY_COMPLETE check_avr 0", "TIDY_COMPLETE check_avr -11")) == 1


def test_empty_input_and_duplicate_completions_fail() -> None:
    assert analysis_check.incomplete_environments("TIDY_COMPLETE check_avr 0 0\n", ["check_avr"]) == ["check_avr"]
    assert analysis_check.incomplete_environments("TIDY_COMPLETE check_avr 0 1\n" * 2, ["check_avr"]) == ["check_avr"]


def test_exit_one_without_compiler_errors_fails(monkeypatch: object) -> None:
    assert _run_main(monkeypatch, VERBOSE_OUTPUT.replace("TIDY_COMPLETE check_avr 0", "TIDY_COMPLETE check_avr 1")) == 1


def test_a_stale_exclusion_fails_the_gate(monkeypatch: object, capsys: object) -> None:
    assert _run_main(monkeypatch, VERBOSE_OUTPUT + "TIDY_STALE check_avr lib/configHandler/*\n") == 1
    assert "check_avr: lib/configHandler/*" in capsys.readouterr().out   # type: ignore[attr-defined]
