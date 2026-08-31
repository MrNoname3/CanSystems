#!/usr/bin/env python3
"""Dependency guard for the release gate: the .venv matches the pins.

Every gate guard runs a tool out of the project-root .venv, so a .venv that has drifted from
requirements-dev.txt makes the gate report on versions the project does not pin - a stale ruff
lints with rules the pinned one does not have, and the run still passes.

Requirement lines are parsed by `packaging`, the PyPA reference implementation of PEP 440/508,
rather than by a regex of our own; only the `-r` include is handled here, since that is a
pip requirements-file feature and not part of the requirement syntax. Constraints are checked
through the parsed specifier, so a `>=` pin is compared rather than skipped.

requirements-ci.txt is deliberately not read: platformio and intelhex are installed only in CI,
and PlatformIO itself runs from its own penv.

`--sync` installs the pinned set before checking, instead of only reporting the difference.
"""

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
VENV_PYTHON = PROJECT_DIR / ".venv" / "bin" / "python"
REQUIREMENTS = PROJECT_DIR / "requirements-dev.txt"


def read_requirements(path: Path, seen: set[Path] | None = None) -> list[str]:
    """Collect the requirement lines of a file and of the files it includes with `-r`."""
    seen = set() if seen is None else seen
    resolved = path.resolve()
    if resolved in seen or not resolved.exists():
        return []
    seen.add(resolved)
    lines: list[str] = []
    for raw in resolved.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith(("-r", "--requirement")):
            included = line.split(maxsplit=1)[1]
            lines.extend(read_requirements(resolved.parent / included, seen))
        else:
            lines.append(line)
    return lines


def sync() -> int:
    print(f"deps: installing {REQUIREMENTS.name} into .venv", flush=True)
    pip = PROJECT_DIR / ".venv" / "bin" / "pip"
    return subprocess.run([str(pip), "install", "-r", str(REQUIREMENTS)],
                          cwd=PROJECT_DIR, check=False).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Check the .venv against the pinned tooling.")
    parser.add_argument("--sync", action="store_true",
                        help="install the pinned set first instead of only reporting")
    args = parser.parse_args()

    if not VENV_PYTHON.exists():
        sys.exit(f"deps: no .venv at {PROJECT_DIR / '.venv'} "
                 f"(python -m venv .venv && .venv/bin/pip install -r {REQUIREMENTS.name})")

    # Re-run under the .venv interpreter, so both `packaging` and the metadata lookup below
    # answer for the environment the other guards actually use.
    if Path(sys.executable).resolve() != VENV_PYTHON.resolve():
        return subprocess.run([str(VENV_PYTHON), __file__, *sys.argv[1:]], check=False).returncode

    from importlib.metadata import PackageNotFoundError, version

    from packaging.requirements import Requirement
    from packaging.utils import canonicalize_name

    if args.sync:
        failure = sync()
        if failure != 0:
            return failure

    requirements = [Requirement(line) for line in read_requirements(REQUIREMENTS)]
    if not requirements:
        sys.exit(f"deps: no requirements found in {REQUIREMENTS.name}")

    drifted: list[str] = []
    for requirement in sorted(requirements, key=lambda r: canonicalize_name(r.name)):
        try:
            installed = version(requirement.name)
        except PackageNotFoundError:
            drifted.append(f"  {requirement}: not installed")
            continue
        if not requirement.specifier.contains(installed, prereleases=True):
            drifted.append(f"  {requirement}: installed {installed}")

    if drifted:
        print(f"deps: {len(drifted)} of {len(requirements)} requirement(s) not satisfied by .venv:")
        print("\n".join(drifted))
        print(f"\nFix with: .venv/bin/pip install -r {REQUIREMENTS.name}"
              f"  /  scripts/release_check.py --sync")
        return 1

    print(f"deps: all {len(requirements)} pinned package(s) match .venv", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
