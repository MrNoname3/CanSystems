"""Resolve every guard's executable from the environment whose pins deps_check validates."""

import os
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
VENV_PYTHON = PROJECT_DIR / ".venv" / "bin" / "python"


def target_python(venv_python: Path = VENV_PYTHON) -> Path:
    return venv_python if venv_python.exists() else Path(sys.executable)


def find_tool(name: str) -> str:
    """Use the pinned environment, never an unrelated executable earlier on PATH.

    Legacy overrides can select aliases of the same executable, but cannot silently escape the
    environment whose distribution versions the gate checked.
    """
    expected = target_python().parent / name
    override = os.environ.get(name.upper().replace("-", "_"))
    if override and Path(override).resolve() != expected.resolve():
        sys.exit(f"{name}: override {override} is outside the pinned tooling environment ({expected})")
    if not expected.is_file() or not os.access(expected, os.X_OK):
        sys.exit(f"{name} not found at {expected}; install requirements-dev.txt into the tooling environment")
    return str(expected)
