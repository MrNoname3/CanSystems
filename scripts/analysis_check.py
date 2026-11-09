#!/usr/bin/env python3
"""Static-analysis guard for the release gate: runs clang-tidy and checks that it ran.

`pio check` cannot fail on a clang-tidy that never analysed anything. It drops every
[clang-diagnostic-error] line instead of counting it as a defect, and it treats the tool's exit
code as a success unless it is 2 or more - so a segfault, which reports a negative code, comes
back as PASSED with no output at all. So pio_tidy.py replaces PlatformIO's clang-tidy driver: it
checks the tool's actual exit status, and prints a TIDY_COMPLETE record only after the process
has exited. A diagnostic is no such evidence: a tool can crash after its first warning, or
complete without any.

A clang error in a project file is a blind spot as well: clang-tidy skips whatever depends on
the declaration it could not parse. So every one fails the gate. A file written for another target,
which cannot parse in an environment, is left out of it by PLATFORM_EXCLUSIONS, with the reason.

The defects are counted here too, those in project files only. clang-tidy reports a finding in a
library header whenever the path leading to it starts in project code, and no header filter drops
it; the libraries are not this project's to fix, as cppcheck's --suppress=*:*.pio/* says as well.
`pio check`'s exit code then speaks for the tool alone.

The clang-tidy it runs is not pinned in platformio.ini: the pioarduino platform installs its own,
and platform_packages cannot hold it (see the note there). So the version is checked here, and a
platform bump that brings another one fails until CLANG_TIDY_VERSION follows it.

`--env` narrows the run to some of the check_* environments, so CI can give each its own job; the
checks above then apply to those alone.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Sequence
from fnmatch import fnmatch
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
ENVIRONMENTS = ("check_avr", "check_esp8266", "check_esp32")
CLANG_TIDY_VERSION = "21.1.0"

# Per environment, the project files left out of it, as (glob, reason) pairs: files written for
# another target, whose headers or libraries this one does not have. Another target's main is left
# out by ENTRYPOINTS instead.
ESP_NETWORK = "the ESP network and MQTT stack: no connectivity, <pgmspace.h> or C++ library on AVR"
ESP_STORAGE = "the ESP configuration store: no LittleFS or C++ library on AVR"
AVR_WIRE = "an ATmega328P sensor: Wire.setWireTimeout() is the AVR core's"
AVR_OTA = "the ATmega328P's OTA, sized by PROGRAM_MEMORY_SIZE"
AVR_LED = "the ATmega328P nodes' LED, on NeoPixelBus"
PLATFORM_EXCLUSIONS: dict[str, tuple[tuple[str, str], ...]] = {
    "check_avr": (
        ("lib/canAlertDriver/*", ESP_NETWORK),
        ("lib/messageRoute/*", ESP_NETWORK),
        ("lib/canMqttGateway/*", ESP_NETWORK),
        ("lib/connectivity/*", ESP_NETWORK),
        ("lib/haDiscovery/*", ESP_NETWORK),
        ("lib/mqttCommon/*", ESP_NETWORK),
        ("lib/mqttThermometer/*", ESP_NETWORK),
        ("lib/mqttTopics/*", ESP_NETWORK),
        ("lib/networkHandler/*", ESP_NETWORK),
        ("lib/otaRegistry/*", ESP_NETWORK),
        ("lib/pubSubClient/*", ESP_NETWORK),
        ("lib/rfHandler/*", ESP_NETWORK),
        ("lib/configHandler/*", ESP_STORAGE),
        ("lib/dataTransfer/*", ESP_STORAGE),
    ),
    "check_esp8266": (
        ("lib/CANDriver/*", "CAN: the ESP8266 core's SPI has no usingInterrupt()"),
        ("lib/canAlertDriver/*", "CAN: canHandler declares CanBase for the ATmega328P and the ESP32"),
        ("lib/canMqttGateway/*", "CAN: canHandler declares CanBase for the ATmega328P and the ESP32"),
        ("lib/ambientSensor/*", AVR_WIRE),
        ("lib/pcf8574/*", AVR_WIRE),
        ("lib/ota/*", AVR_OTA),
        ("lib/rgbLedWrapper/*", AVR_LED),
    ),
    "check_esp32": (
        ("lib/ambientSensor/*", AVR_WIRE),
        ("lib/pcf8574/*", AVR_WIRE),
        ("lib/ota/*", AVR_OTA),
        ("lib/canHandler/src/otaCanResponse.hpp", AVR_OTA),
        ("lib/dfPlayer/*", "the ATmega328P's DFPlayer, on the AVR core's SoftwareSerial"),
    ),
}

ENTRYPOINTS = {
    "check_avr": ("main_atmega328_alert.cpp", "main_atmega328_irrigation.cpp"),
    "check_esp32": ("main_esp32_can.cpp",),
    "check_esp8266": ("main_esp8266_rad.cpp", "main_esp8266_thermo.cpp"),
}


def target_inputs(environment: str, inputs: Sequence[str]) -> list[str]:
    """All compatible library files, including unused ones, and this target's entrypoints."""
    selected: list[str] = []
    for source in inputs:
        path = project_file(source)
        if path is None:
            continue
        if path.startswith("src/") and Path(path).name not in (*ENTRYPOINTS[environment], "main.cpp"):
            continue
        if not any(fnmatch(path, pattern) for pattern, _ in PLATFORM_EXCLUSIONS[environment]):
            selected.append(source)
    return selected


def uncovered_inputs() -> list[str]:
    """No own library/source may disappear from every target when exclusions change."""
    inputs = [str(path) for folder in ("lib", "src") for path in (PROJECT_DIR / folder).rglob("*")
              if path.suffix in (".c", ".cc", ".cpp", ".cxx", ".ino", ".h", ".hh", ".hpp", ".hxx")
              and path.name != "main_native.cpp"]
    covered = {source for env in ENVIRONMENTS for source in target_inputs(env, inputs)}
    return sorted(str(Path(source).relative_to(PROJECT_DIR)) for source in inputs if source not in covered)


ENVIRONMENT_HEADING = re.compile(r"^Checking (\S+) > clangtidy ")
CLANG_ERROR = re.compile(r"^(\S+?):(\d+):\d+: error: (.*) \[clang-diagnostic-error\]$")
DEFECT = re.compile(r"^(\S+?):\d+: \[(?:low|medium|high):\w+\] ")
DIAGNOSTIC = re.compile(r": (?:error|warning|note): ")
NOT_FOUND = re.compile(r"'([^']+)' file not found")
TOOL_COMMAND = re.compile(r"^(\S*clang-tidy) ", re.MULTILINE)
LLVM_VERSION = re.compile(r"LLVM version (\S+)")


def find_pio() -> str:
    """Locate the PlatformIO CLI: PATH first, then the standard penv location."""
    pio = shutil.which("pio")
    if pio is not None:
        return pio
    fallback = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if fallback.exists():
        return str(fallback)
    sys.exit("pio executable not found (PATH and ~/.platformio/penv/bin/pio checked)")


def run_check(pio: str, environments: Sequence[str]) -> tuple[int, str]:
    """Run the clang-tidy environments verbosely; return the exit code and the output."""
    command = [str(Path(pio).parent / "python"), str(PROJECT_DIR / "scripts" / "pio_tidy.py"),
               "check", "--verbose"]
    for environment in environments:
        command += ["-e", environment]
    log_path = PROJECT_DIR / ".pio" / "analysis" / "driver.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        with subprocess.Popen(command, cwd=PROJECT_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, env={**os.environ, "VIRTUAL_ENV": ""}) as process:
            assert process.stdout is not None
            for line in process.stdout:
                log.write(line)
                if line.startswith("analysis:"):
                    print(line, end="", flush=True)
            status = process.wait()
    return status, log_path.read_text(encoding="utf-8")


def clang_tidy_binary(output: str) -> str | None:
    """The clang-tidy executable `pio check --verbose` echoed running, if it ran one at all."""
    match = TOOL_COMMAND.search(output)
    return match.group(1) if match is not None else None


def parse_version(version_output: str) -> str | None:
    """The LLVM version `clang-tidy --version` reports."""
    match = LLVM_VERSION.search(version_output)
    return match.group(1) if match is not None else None


def clang_tidy_version(binary: str) -> str | None:
    """Ask the clang-tidy that ran for its version."""
    result = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False)
    return parse_version(result.stdout + result.stderr)


def diagnostics_per_environment(output: str,
                                 environments: Sequence[str] = ENVIRONMENTS) -> dict[str, list[str]]:
    """Split the verbose output into the diagnostic lines each environment produced."""
    per_environment: dict[str, list[str]] = {name: [] for name in environments}
    current: str | None = None
    for line in output.splitlines():
        heading = ENVIRONMENT_HEADING.match(line)
        if heading is not None:
            current = heading.group(1)
            continue
        if current in per_environment and DIAGNOSTIC.search(line):
            per_environment[current].append(line)
    return per_environment


def report(per_environment: dict[str, list[str]]) -> None:
    """Summarize diagnostics; silence is valid when completion is independently verified."""
    for environment, lines in per_environment.items():
        unresolved = sorted({match.group(1) for line in lines
                             for match in [NOT_FOUND.search(line)] if match is not None})
        print(f"analysis: {environment} - {len(lines)} diagnostic(s)"
              + (f", headers it could not find: {', '.join(unresolved)}" if unresolved else ""))


def incomplete_environments(output: str, environments: Sequence[str]) -> list[str]:
    """Require exactly one successful completion with nonempty input for each environment."""
    records = re.findall(r"^TIDY_COMPLETE (\S+) (-?\d+) (\d+)$", output, re.MULTILINE)
    diagnostics = diagnostics_per_environment(output, environments)
    return [env for env in environments
            if len([record for record in records if record[0] == env]) != 1
            or not any(name == env and int(count) > 0
                       and (int(status) == 0 or (int(status) == 1
                            and any(CLANG_ERROR.match(line) for line in diagnostics[env])))
                       for name, status, count in records)]


def project_file(path: str) -> str | None:
    """The path relative to the project, if it is one of the project's own files."""
    try:
        relative = (PROJECT_DIR / path).resolve().relative_to(PROJECT_DIR.resolve())
    except ValueError:
        return None
    return None if relative.parts[0] == ".pio" else relative.as_posix()


def check_errors(per_environment: dict[str, list[str]]) -> list[str]:
    """The clang errors in project files."""
    errors: list[str] = []
    for environment, lines in per_environment.items():
        for line in lines:
            match = CLANG_ERROR.match(line)
            path = None if match is None else project_file(match.group(1))
            if match is not None and path is not None:
                errors.append(f"{environment}: {path}:{match.group(2)}: {match.group(3)}")
    return errors


def project_defects(output: str) -> tuple[list[str], int]:
    """The defect lines `pio check` printed for project files, and how many it printed for others."""
    own: list[str] = []
    outside = 0
    for line in output.splitlines():
        match = DEFECT.match(line)
        if match is None:
            continue
        if project_file(match.group(1)) is None:
            outside += 1
        else:
            own.append(line)
    return own, outside


def main(argv: Sequence[str] = ()) -> int:
    parser = argparse.ArgumentParser(description="clang-tidy over the check_* environments.")
    parser.add_argument("--env", nargs="+", choices=ENVIRONMENTS, default=list(ENVIRONMENTS),
                        metavar="ENV", help=f"environments to analyse (default: {', '.join(ENVIRONMENTS)})")
    environments: list[str] = parser.parse_args(list(argv)).env

    uncovered = uncovered_inputs()
    if uncovered:
        print(f"analysis: files excluded from every target: {uncovered}")
        return 1
    pio = find_pio()
    status, output = run_check(pio, environments)

    per_environment = diagnostics_per_environment(output, environments)
    report(per_environment)
    incomplete = incomplete_environments(output, environments)

    if incomplete:
        print(f"analysis: missing or failed clang-tidy completion: {', '.join(incomplete)}")
        print(output)
        return 1

    binary = clang_tidy_binary(output)
    if binary is None:
        print("\nanalysis: the clang-tidy command is missing from `pio check --verbose`'s output, "
              "so its version cannot be checked.")
        return 1
    version = clang_tidy_version(binary)
    print(f"analysis: clang-tidy {version} ({binary})")
    if version != CLANG_TIDY_VERSION:
        print(f"\nanalysis: this is not the clang-tidy {CLANG_TIDY_VERSION} the project was set up "
              f"with. A platform bump brought it: go through what the new version reports, then "
              f"update CLANG_TIDY_VERSION in {Path(__file__).name}.")
        return 1

    defects, outside = project_defects(output)
    if outside:
        print(f"analysis: {outside} defect(s) in library and framework code left out")
    if status != 0 or defects:
        # From the first environment heading on: what precedes it is the command line pio echoes
        # under --verbose, which is thousands of characters of -D and -I.
        start = output.find("Checking ")
        print(output[start:] if start >= 0 else output)
    if defects:
        print(f"\nanalysis: {len(defects)} defect(s) in project code:")
        for defect in defects:
            print(f"  {defect}")

    errors = check_errors(per_environment)
    if errors:
        print("\nanalysis: clang could not parse project code, so clang-tidy skipped what depends on it:")
        for error in errors:
            print(f"  {error}")
        print("Fix the include paths or flags of the check_* environment, or, for a file written for "
              f"another target, add it to PLATFORM_EXCLUSIONS in {Path(__file__).name} with the reason.")
        return 1
    return 1 if defects else status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
