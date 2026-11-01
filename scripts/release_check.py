#!/usr/bin/env python3
"""Release gate: build all environments, run the native test suite, then static analysis.

Runs the commands a release would need anyway, fail-fast, in this order:
  1. deps_check.py              (the .venv matches requirements-dev.txt; ANY drift fails)
  2. pio run                    (build every default environment)
  3. pio test -e native_test    (native unit-test suite)
  4. pio check --fail-on-defect (cppcheck over every environment; ANY defect fails)
  5. analysis_check.py          (clang-tidy over the check_* environments; ANY project defect fails)
  6. format_check.py            (clang-format + final-newline check; ANY violation fails)
  7. lint_check.py              (ruff check over the Python; ANY finding fails)
  8. workflow_check.py          (actionlint + zizmor over .github; ANY finding fails)
  9. typecheck_check.py         (pyright strict over the Python; ANY error fails)
 10. pytest_check.py            (pytest over ota/tests + scripts/tests; ANY failure fails)

The analysers are separate steps because they need opposite check_skip_packages settings; the
check_* environments in platformio.ini say why.

Every guard is required: a missing tool fails the gate rather than skipping its step, so a
green run means all ten actually ran. The dependency step comes first because the guards
after it run out of the .venv: a drifted one reports on versions the project does not pin.
`--sync` installs the pinned set instead of only reporting the drift.

Step 0 checks the git working tree with the same rule the firmware build uses for its
GIT_DIRTY flag (scripts/git_utils.py): dirty is a warning by default, a failure with --strict.
It also checks that the CI groups in gate_groups.py cover every environment exactly once.

`--group NAME` runs one of those groups instead of the whole gate: CI gives each its own job.

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

import analysis_check
from build_jobs import jobs_args
from gate_groups import GROUPS, Group, coverage_gaps, default_envs
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

    def __init__(self, name: str, argv: list[str]):
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


def gate_steps(group: Group | None, pio: str, deps_cmd: list[str], jobs: list[str]) -> list[Step]:
    """The steps of the whole gate, or those of one CI group, in the whole gate's order; jobs are
    the build's --jobs arguments."""
    def script(name: str, *args: str) -> list[str]:
        return [sys.executable, str(PROJECT_DIR / "scripts" / name), *args]

    host = group is None or group.host
    builds = group is None or bool(group.envs)
    tidies = group is None or bool(group.tidy)
    envs = [] if group is None else [arg for env in group.envs for arg in ("-e", env)]
    tidy = [] if group is None else ["--env", *group.tidy]
    steps = [
        (host, Step("deps", deps_cmd)),
        (builds, Step("build", [pio, "run", *envs, *jobs])),
        (host, Step("test",  [pio, "test", "-e", "native_test"])),
        (builds, Step("check", [pio, "check", *envs,
                                "--fail-on-defect", "low",
                                "--fail-on-defect", "medium",
                                "--fail-on-defect", "high"])),
        (tidies, Step("tidy", script("analysis_check.py", *tidy))),
        (host, Step("format", script("format_check.py"))),
        (host, Step("lint", script("lint_check.py"))),
        (host, Step("workflows", script("workflow_check.py"))),
        (host, Step("typecheck", script("typecheck_check.py"))),
        (host, Step("pytest", script("pytest_check.py"))),
    ]
    return [step for wanted, step in steps if wanted]


def main() -> int:
    parser = argparse.ArgumentParser(description="Pre-release gate: build + test + check.")
    parser.add_argument("--strict", action="store_true",
                        help="fail on a dirty git tree instead of warning")
    parser.add_argument("--sync", action="store_true",
                        help="install the pinned tooling into .venv before checking it")
    parser.add_argument("--group", choices=GROUPS, metavar="NAME",
                        help=f"run one CI group of the gate: {', '.join(GROUPS)}")
    args = parser.parse_args()

    os.chdir(PROJECT_DIR)
    os.environ.pop("VIRTUAL_ENV", None)               # a project .venv breaks pio's venv detection.
    pio = find_pio()

    # Step 0: same dirty rule as the firmware's GIT_DIRTY build flag.
    dirty = bool(get_git_dirty())
    dirty_note = "dirty working tree (firmware would report dirty=1)" if dirty else "clean"
    if dirty and args.strict:
        print(f"git status   FAIL   {dirty_note}")
        print("RELEASE CHECK: FAIL (step: git status)")
        return 1

    gaps = coverage_gaps(default_envs(PROJECT_DIR / "platformio.ini"), analysis_check.ENVIRONMENTS)
    if gaps:
        for gap in gaps:
            print(f"groups       FAIL   {gap}")
        print("Put it in exactly one group in scripts/gate_groups.py, so a CI job runs it.")
        print("RELEASE CHECK: FAIL (step: groups)")
        return 1

    deps_cmd = [sys.executable, str(PROJECT_DIR / "scripts" / "deps_check.py")]
    if args.sync:
        deps_cmd.append("--sync")

    steps = gate_steps(None if args.group is None else GROUPS[args.group], pio, deps_cmd, jobs_args())

    failed = None
    for step in steps:
        if not step.run():
            failed = step
            break

    # --- Machine-friendly epilogue: summary table, failed-step replay, final marker. ---
    print(f"\n{' RELEASE CHECK SUMMARY '.center(78, '=')}")
    print(f"  git status   {'WARN' if dirty else 'PASS':4s}   {dirty_note}")
    print("  groups       PASS   every environment in one CI group"
          + (f"; this run: {args.group}" if args.group else ""))
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
