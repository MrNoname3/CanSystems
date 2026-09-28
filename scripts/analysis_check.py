#!/usr/bin/env python3
"""Static-analysis guard for the release gate: runs clang-tidy and checks that it ran.

`pio check` cannot fail on a clang-tidy that never analysed anything. It drops every
[clang-diagnostic-error] line instead of counting it as a defect, and it treats the tool's exit
code as a success unless it is 2 or more - so a segfault, which reports a negative code, comes
back as PASSED with no output at all. Both failures have happened here: one from missing include
paths, one from a predefined macro PlatformIO passes through. This guard runs the same
`pio check` and looks for what each of them leaves behind.

A clang error in a project file is a blind spot as well: clang-tidy skips whatever depends on
the declaration it could not parse. So every one fails the gate, unless EXPECTED_ERRORS names the
file for that environment and says why it cannot parse there.

The defects are counted here too, those in project files only. clang-tidy reports a finding in a
library header whenever the path leading to it starts in project code, and no header filter drops
it; the libraries are not this project's to fix, as cppcheck's --suppress=*:*.pio/* says as well.
`pio check`'s exit code then speaks for the tool alone.

The clang-tidy it runs is not pinned in platformio.ini: the pioarduino platform installs its own,
and platform_packages cannot hold it (see the note there). So the version is checked here, and a
platform bump that brings another one fails until CLANG_TIDY_VERSION follows it.
"""

import os
import re
import shutil
import subprocess
import sys
from fnmatch import fnmatch
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
ENVIRONMENTS = ("check_avr", "check_esp8266", "check_esp32")
CLANG_TIDY_VERSION = "21.1.0"

# Per environment, the project files clang is expected to fail on, as (glob, reason) pairs: sources
# written for another target, whose headers or libraries this one does not have.
ESP_NETWORK = "the ESP network and MQTT stack: no connectivity, <pgmspace.h> or C++ library on AVR"
ESP_STORAGE = "the ESP configuration store: no LittleFS or C++ library on AVR"
AVR_WIRE = "an ATmega328P sensor: Wire.setWireTimeout() is the AVR core's"
AVR_OTA = "the ATmega328P's OTA, sized by PROGRAM_MEMORY_SIZE"
AVR_LED = "the ATmega328P nodes' LED, on NeoPixelBus"
EXPECTED_ERRORS: dict[str, tuple[tuple[str, str], ...]] = {
    "check_avr": (
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
        ("src/main_esp*.cpp", ESP_NETWORK),
        ("lib/configHandler/*", ESP_STORAGE),
        ("lib/dataTransfer/*", ESP_STORAGE),
    ),
    "check_esp8266": (
        ("lib/CANDriver/*", "CAN: the ESP8266 core's SPI has no usingInterrupt()"),
        ("lib/canAlertDriver/*", "CAN: canHandler declares CanBase for the ATmega328P and the ESP32"),
        ("lib/canMqttGateway/*", "CAN: canHandler declares CanBase for the ATmega328P and the ESP32"),
        ("src/main_esp32_can.cpp", "CAN: canHandler declares CanBase for the ATmega328P and the ESP32"),
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
        ("src/main_esp8266_*.cpp", "the ESP8266 board's pin names and Connectivity constructor"),
        ("src/main_atmega328_*.cpp", "the ATmega328P nodes' pins, watchdog and CAN handler"),
    ),
}

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


def run_check(pio: str) -> tuple[int, str]:
    """Run the clang-tidy environments verbosely; return the exit code and the output."""
    command = [pio, "check", "--verbose"]
    for environment in ENVIRONMENTS:
        command += ["-e", environment]
    result = subprocess.run(command, cwd=PROJECT_DIR, capture_output=True, text=True,
                            check=False, env={**os.environ, "VIRTUAL_ENV": ""})
    return result.returncode, result.stdout + result.stderr


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


def diagnostics_per_environment(output: str) -> dict[str, list[str]]:
    """Split the verbose output into the diagnostic lines each environment produced."""
    per_environment: dict[str, list[str]] = {name: [] for name in ENVIRONMENTS}
    current: str | None = None
    for line in output.splitlines():
        heading = ENVIRONMENT_HEADING.match(line)
        if heading is not None:
            current = heading.group(1)
            continue
        if current in per_environment and DIAGNOSTIC.search(line):
            per_environment[current].append(line)
    return per_environment


def report(per_environment: dict[str, list[str]]) -> list[str]:
    """Print what clang-tidy could not parse; return the environments that did not run."""
    silent: list[str] = []
    for environment, lines in per_environment.items():
        unresolved = sorted({match.group(1) for line in lines
                             for match in [NOT_FOUND.search(line)] if match is not None})
        print(f"analysis: {environment} - {len(lines)} diagnostic(s)"
              + (f", headers it could not find: {', '.join(unresolved)}" if unresolved else ""))
        if not lines:
            silent.append(environment)
    return silent


def project_file(path: str) -> str | None:
    """The path relative to the project, if it is one of the project's own files."""
    try:
        relative = (PROJECT_DIR / path).resolve().relative_to(PROJECT_DIR.resolve())
    except ValueError:
        return None
    return None if relative.parts[0] == ".pio" else relative.as_posix()


def check_errors(per_environment: dict[str, list[str]]) -> tuple[list[str], list[str]]:
    """The clang errors in project files that EXPECTED_ERRORS does not account for, and the
    EXPECTED_ERRORS entries no error matched any more."""
    unexpected: list[str] = []
    unused: list[str] = []
    for environment, lines in per_environment.items():
        expected = EXPECTED_ERRORS.get(environment, ())
        matched: set[str] = set()
        for line in lines:
            match = CLANG_ERROR.match(line)
            if match is None:
                continue
            path = project_file(match.group(1))
            if path is None:
                continue
            patterns = [pattern for pattern, _ in expected if fnmatch(path, pattern)]
            if patterns:
                matched.update(patterns)
            else:
                unexpected.append(f"{environment}: {path}:{match.group(2)}: {match.group(3)}")
        unused += [f"{environment}: {pattern}" for pattern, _ in expected if pattern not in matched]
    return unexpected, unused


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


def main() -> int:
    pio = find_pio()
    status, output = run_check(pio)

    per_environment = diagnostics_per_environment(output)
    silent = report(per_environment)

    if silent:
        print(f"\nanalysis: clang-tidy analysed nothing in {', '.join(silent)} - `pio check` "
              f"reports that as a pass, so the gate has to catch it here.")
        print("Run `pio check --verbose -e <environment>` and check the include paths and the "
              "--target in platformio.ini's check_* environments.")
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

    unexpected, unused = check_errors(per_environment)
    if unused:
        print(f"analysis: EXPECTED_ERRORS entries nothing matched, to take out: {', '.join(unused)}")
    if unexpected:
        print("\nanalysis: clang could not parse project code, so clang-tidy skipped what depends on it:")
        for error in unexpected:
            print(f"  {error}")
        print("Fix the include paths or flags of the check_* environment, or, for a source written for "
              f"another target, add it to EXPECTED_ERRORS in {Path(__file__).name} with the reason.")
        return 1
    return 1 if defects else status


if __name__ == "__main__":
    sys.exit(main())
