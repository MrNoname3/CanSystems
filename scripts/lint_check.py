#!/usr/bin/env python3
"""Lint guard for the release gate: `ruff check` over the project's Python.

Runs ruff (using the config in pyproject.toml) over ota/, scripts/, test/ and web/. ruff is
required: if it is not found the gate fails (install requirements-dev.txt into .venv).
Only linting is run; `ruff format` is deliberately not used (see pyproject.toml).

The executable comes from the environment checked by deps_check.py: the project .venv
when present, otherwise the running interpreter's bin directory.
"""

import subprocess
import sys
from pathlib import Path

from tooling import find_tool

PROJECT_DIR = Path(__file__).resolve().parent.parent
TARGETS = ["ota", "scripts", "test", "web"]


def find_ruff() -> str:
    return find_tool("ruff")


def main() -> int:
    ruff = find_ruff()
    version = subprocess.run([ruff, "--version"], capture_output=True, text=True, check=False).stdout.strip()
    print(f"lint: {version}", flush=True)
    return subprocess.run([ruff, "check", *TARGETS], cwd=PROJECT_DIR, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
