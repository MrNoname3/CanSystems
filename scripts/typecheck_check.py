#!/usr/bin/env python3
"""Type-check guard for the release gate: `pyright` over the project's Python.

Runs pyright (config in pyproject.toml's [tool.pyright]) over the OTA tool and the dev
scripts in strict mode. pyright is required: if it is not found the gate fails (install
requirements-dev.txt into .venv). The four PlatformIO SCons build scripts are excluded in
the config (they rely on Import()/env names pyright cannot see).

The executable comes from the environment checked by deps_check.py: the project .venv
when present, otherwise the running interpreter's bin directory.
"""

import subprocess
import sys
from pathlib import Path

from tooling import find_tool

PROJECT_DIR = Path(__file__).resolve().parent.parent
VENV = PROJECT_DIR / ".venv"


def find_pyright() -> str:
    return find_tool("pyright")


def deps_python() -> str:
    """Interpreter that has the OTA tool's third-party deps, for pyright's import resolution."""
    venv_python = VENV / "bin" / "python"
    return str(venv_python) if venv_python.exists() else sys.executable


def main() -> int:
    pyright = find_pyright()
    version = subprocess.run([pyright, "--version"], capture_output=True, text=True, check=False).stdout.strip()
    print(f"typecheck: {version}", flush=True)
    return subprocess.run([pyright, "--pythonpath", deps_python()], cwd=PROJECT_DIR, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
