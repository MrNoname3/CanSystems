#!/usr/bin/env python3
"""Static-analysis guard for the release gate: runs clang-tidy and checks that it ran.

`pio check` cannot fail on a clang-tidy that never analysed anything. It drops every
[clang-diagnostic-error] line instead of counting it as a defect, and it treats the tool's exit
code as a success unless it is 2 or more - so a segfault, which reports a negative code, comes
back as PASSED with no output at all. Both failures have happened here: one from missing include
paths, one from a predefined macro PlatformIO passes through. This guard runs the same
`pio check` and looks for what each of them leaves behind.

Defects themselves are still `pio check`'s business; its exit code is passed straight on.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
ENVIRONMENTS = ("check_avr", "check_esp8266", "check_esp32")

ENVIRONMENT_HEADING = re.compile(r"^Checking (\S+) > clangtidy ")
DIAGNOSTIC = re.compile(r": (?:error|warning|note): ")
NOT_FOUND = re.compile(r"'([^']+)' file not found")


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
    for severity in ("low", "medium", "high"):
        command += ["--fail-on-defect", severity]
    result = subprocess.run(command, cwd=PROJECT_DIR, capture_output=True, text=True,
                            check=False, env={**os.environ, "VIRTUAL_ENV": ""})
    return result.returncode, result.stdout + result.stderr


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

    if status != 0:
        # From the first environment heading on: what precedes it is the command line pio echoes
        # under --verbose, which is thousands of characters of -D and -I.
        start = output.find("Checking ")
        print(output[start:] if start >= 0 else output)
    return status


if __name__ == "__main__":
    sys.exit(main())
