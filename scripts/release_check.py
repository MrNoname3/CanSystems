#!/usr/bin/env python3
"""Release gate: build all environments, run the native test suite, then static analysis.

Runs the three commands a release would need anyway, fail-fast, in this order:
  1. pio run                    (build every default environment)
  2. pio test -e native_test    (native unit-test suite)
  3. pio check --fail-on-defect (cppcheck + clang-tidy; ANY defect fails)

Step 0 checks the git working tree with the same rule the firmware build uses for its
GIT_DIRTY flag (scripts/git_utils.py): dirty is a warning by default, a failure with --strict.

Each command's output streams through a pseudo-terminal, so a human sees exactly what a
standalone run would print (colors included). The output is also captured: the run ends with
a summary table, a replay of the failed step's last lines, and a single machine-friendly
"RELEASE CHECK: PASS|FAIL" marker line — tail-ing the output is enough to know everything.
"""

import argparse
import os
import pty
import re
import shutil
import sys
import time
from pathlib import Path

from git_utils import get_git_dirty

PROJECT_DIR = Path(__file__).resolve().parent.parent
TAIL_LINES = 50                                       # Replayed lines of a failed step.
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\r")    # ANSI escape sequences and bare CRs.


def find_pio() -> str:
    """Locate the PlatformIO CLI: PATH first, then the standard penv location."""
    pio = shutil.which("pio")
    if pio is not None:
        return pio
    fallback = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if fallback.exists():
        return str(fallback)
    sys.exit("pio executable not found (PATH and ~/.platformio/penv/bin/pio checked)")


def format_duration(seconds: float) -> str:
    return f"{int(seconds) // 60}:{int(seconds) % 60:02d}"


class Step:
    """One gate step: runs a command through a pty, streaming and capturing its output."""

    def __init__(self, name: str, argv: list):
        self.name = name
        self.argv = argv
        self.passed = False
        self.duration = 0.0
        self.output = bytearray()

    def run(self) -> bool:
        print(f"\n{f' {self.name} '.center(78, '=')}")
        print(f"$ {' '.join(self.argv)}\n", flush=True)

        def master_read(fd: int) -> bytes:
            data = os.read(fd, 4096)
            self.output.extend(data)
            return data                               # pty.spawn() relays this to stdout.

        start = time.monotonic()
        status = pty.spawn(self.argv, master_read)
        self.duration = time.monotonic() - start
        self.passed = (os.waitstatus_to_exitcode(status) == 0)
        return self.passed

    def tail(self) -> str:
        """Last TAIL_LINES of the captured output, with ANSI noise stripped."""
        text = ANSI_RE.sub("", self.output.decode("utf-8", errors="replace"))
        lines = [line for line in text.splitlines() if line.strip()]
        return "\n".join(lines[-TAIL_LINES:])


def main() -> int:
    parser = argparse.ArgumentParser(description="Pre-release gate: build + test + check.")
    parser.add_argument("--strict", action="store_true",
                        help="fail on a dirty git tree instead of warning")
    args = parser.parse_args()

    os.chdir(PROJECT_DIR)
    os.environ.pop("VIRTUAL_ENV", None)               # ota/.venv breaks pio's venv detection.
    pio = find_pio()

    # Step 0: same dirty rule as the firmware's GIT_DIRTY build flag.
    dirty = bool(get_git_dirty())
    dirty_note = "dirty working tree (firmware would report dirty=1)" if dirty else "clean"
    if dirty and args.strict:
        print(f"git status   FAIL   {dirty_note}")
        print("RELEASE CHECK: FAIL (step: git status)")
        return 1

    steps = [
        Step("build", [pio, "run"]),
        Step("test",  [pio, "test", "-e", "native_test"]),
        Step("check", [pio, "check", "--skip-packages",
                       "--fail-on-defect", "low",
                       "--fail-on-defect", "medium",
                       "--fail-on-defect", "high"]),
    ]

    failed = None
    for step in steps:
        if not step.run():
            failed = step
            break

    # --- Machine-friendly epilogue: summary table, failed-step replay, final marker. ---
    print(f"\n{' RELEASE CHECK SUMMARY '.center(78, '=')}")
    print(f"  git status   {'WARN' if dirty else 'PASS':4s}   {dirty_note}")
    for step in steps:
        if step is failed:
            result = "FAIL"
        elif step.passed:
            result = "PASS"
        else:
            result = "SKIP"                            # Not reached due to an earlier failure.
        duration = format_duration(step.duration) if step.duration > 0.0 else "-"
        print(f"  {step.name:12s} {result:4s}   {duration}")
    print("-" * 78)

    if failed is not None:
        print(f"FAILED STEP: {failed.name} — last {TAIL_LINES} lines:\n")
        print(failed.tail())
        print(f"\nRELEASE CHECK: FAIL (step: {failed.name})")
        return 1

    print("RELEASE CHECK: PASS" + (" (warning: dirty tree)" if dirty else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
