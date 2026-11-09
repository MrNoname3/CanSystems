"""Run instrumented native tests and enforce a reviewed line-coverage floor.

One gcovr scan writes all reports. The upstream DFPlayer implementation is excluded; locally
maintained libraries, including modified upstream code, remain in the measurement. The floor
is versioned in ci/coverage-baseline.json and never automatically lowered by CI.
"""

import argparse
import json
import math
import os
import subprocess
import sys
from pathlib import Path
from typing import cast

from analysis_check import find_pio
from build_jobs import jobs_env
from tooling import find_tool

PROJECT_DIR = Path(__file__).resolve().parent.parent
BASELINE = PROJECT_DIR / "ci" / "coverage-baseline.json"


def line_percent(summary: Path) -> float:
    data = cast(dict[str, object], json.loads(summary.read_text()))
    total, covered = data["line_total"], data["line_covered"]
    if not isinstance(total, int) or not isinstance(covered, int) or not 0 <= covered <= total or total == 0:
        raise ValueError("Coverage report contains no valid line measurements")
    return 100 * covered / total


def recorded_floor(percent: float) -> float:
    """A whole percentage a point below the measurement: compilers attribute lines a little differently."""
    return float(math.floor(percent) - 1)


def check_floor(summary: Path, baseline: Path) -> bool:
    current = line_percent(summary)
    data = cast(dict[str, float], json.loads(baseline.read_text()))
    minimum = data["minimum_line_percent"]
    if not math.isfinite(minimum) or not 0 < minimum <= 100:
        raise ValueError("Invalid coverage floor")
    print(f"coverage: {current:.2f}% lines; required >= {minimum:.1f}%", flush=True)
    return current >= minimum


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record-baseline", action="store_true", help="record the measured floor locally for review")
    args = parser.parse_args()
    os.chdir(PROJECT_DIR)
    status = subprocess.run([sys.executable, "scripts/deps_check.py"], check=False).returncode
    if status:
        return status
    pio = find_pio()
    env = {**jobs_env(), **os.environ, "VIRTUAL_ENV": "", "PLATFORMIO_BUILD_FLAGS": "-lgcov --coverage"}
    for command in ([pio, "run", "-e", "native_test", "-t", "clean"], [pio, "test", "-e", "native_test"]):
        status = subprocess.run(command, env=env, check=False).returncode
        if status:
            return status
    directory = PROJECT_DIR / ".pio" / "coverage"
    directory.mkdir(parents=True, exist_ok=True)
    summary = directory / "summary.json"
    status = subprocess.run([find_tool("gcovr"), "--root", ".", "--filter", "lib/",
                             "--exclude", r"lib/dfPlayer/src/DFPlayerMiniFast\.(cpp|h)$",
                             "--json-summary", str(summary), "--markdown", str(directory / "report.md"),
                             "--txt", str(directory / "report.txt")], check=False).returncode
    if status:
        return status
    if args.record_baseline:
        minimum = recorded_floor(line_percent(summary))
        BASELINE.write_text(json.dumps({"minimum_line_percent": minimum}, indent=2) + "\n")
        print(f"coverage: wrote {BASELINE}; review and commit it")
    return 0 if check_floor(summary, BASELINE) else 1


if __name__ == "__main__":
    sys.exit(main())
