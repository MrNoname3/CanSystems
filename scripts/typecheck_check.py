#!/usr/bin/env python3
"""Type-check guard for the release gate: `pyright` over the project's Python.

Runs pyright (config in pyproject.toml's [tool.pyright]) over the OTA tool and the dev
scripts in strict mode. pyright is required: if it is not found the gate fails (install
requirements-dev.txt into .venv). The four PlatformIO SCons build scripts are excluded in
the config (they rely on Import()/env names pyright cannot see).

pyright lookup order: $PYRIGHT, then `pyright` on PATH, then the project-root .venv
(created from requirements-dev.txt). The OTA tool imports third-party deps
(paho-mqtt/pyyaml/tqdm), so pyright is pointed at the interpreter that has them
(--pythonpath): the .venv if present, else the current interpreter.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
VENV = PROJECT_DIR / ".venv"


def find_pyright() -> str:
    override = os.environ.get("PYRIGHT")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("pyright")
    if on_path is not None:
        return on_path
    local = VENV / "bin" / "pyright"
    if local.exists():
        return str(local)
    sys.exit("pyright not found (set $PYRIGHT, put it on PATH, or install requirements-dev.txt into .venv)")


def deps_python() -> str:
    """Interpreter that has the OTA tool's third-party deps, for pyright's import resolution."""
    venv_python = VENV / "bin" / "python"
    return str(venv_python) if venv_python.exists() else sys.executable


def main() -> int:
    pyright = find_pyright()
    version = subprocess.run([pyright, "--version"], capture_output=True, text=True).stdout.strip()
    print(f"typecheck: {version}", flush=True)
    return subprocess.run([pyright, "--pythonpath", deps_python()], cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    sys.exit(main())
