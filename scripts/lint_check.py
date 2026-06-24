#!/usr/bin/env python3
"""Lint guard for the release gate: `ruff check` over the project's Python.

Runs ruff (using the config in pyproject.toml) over ota/, scripts/ and test/. ruff is
required: if it is not found the gate fails (install requirements-dev.txt into .venv).
Only linting is run; `ruff format` is deliberately not used (see pyproject.toml).

ruff lookup order: $RUFF, then `ruff` on PATH, then the project-root .venv.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
TARGETS = ["ota", "scripts", "test"]


def find_ruff() -> str:
    override = os.environ.get("RUFF")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("ruff")
    if on_path is not None:
        return on_path
    local = PROJECT_DIR / ".venv" / "bin" / "ruff"
    if local.exists():
        return str(local)
    sys.exit("ruff not found (set $RUFF, put it on PATH, or install requirements-dev.txt into .venv)")


def main() -> int:
    ruff = find_ruff()
    version = subprocess.run([ruff, "--version"], capture_output=True, text=True).stdout.strip()
    print(f"lint: {version}", flush=True)
    return subprocess.run([ruff, "check", *TARGETS], cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    sys.exit(main())
