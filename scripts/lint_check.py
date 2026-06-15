#!/usr/bin/env python3
"""Lint guard for the release gate: `ruff check` over the project's Python.

Runs ruff (using the config in pyproject.toml) over ota/, scripts/ and test/. If ruff
is not installed it skips with a note and exits 0, so the local gate stays usable without
ruff — CI installs a pinned ruff and enforces it. Only linting is run; `ruff format` is
deliberately not used (see pyproject.toml).

ruff lookup order: $RUFF, then `ruff` on PATH, then a project-local .venv.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
TARGETS = ["ota", "scripts", "test"]


def find_ruff() -> str | None:
    override = os.environ.get("RUFF")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("ruff")
    if on_path is not None:
        return on_path
    local = PROJECT_DIR / ".venv" / "bin" / "ruff"
    return str(local) if local.exists() else None


def main() -> int:
    ruff = find_ruff()
    if ruff is None:
        print("lint: ruff not found - skipping (install ruff, or let CI enforce it)")
        return 0
    version = subprocess.run([ruff, "--version"], capture_output=True, text=True).stdout.strip()
    print(f"lint: {version}", flush=True)
    return subprocess.run([ruff, "check", *TARGETS], cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    sys.exit(main())
