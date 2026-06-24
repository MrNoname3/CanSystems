#!/usr/bin/env python3
"""Test guard for the release gate: run pytest over the Python unit tests (ota/tests).

pytest is required: if it is not found the gate fails (install requirements-dev.txt into
.venv, which also brings the OTA runtime deps the tests import). The tests themselves skip
when the OTA deps (paho-mqtt, tqdm, pyyaml) are absent (pytest.importorskip in ota/tests),
so a pytest-without-deps environment still does not hard-fail mid-run.

pytest lookup order: $PYTEST, then `pytest` on PATH, then the project-root .venv.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent


def find_pytest() -> str:
    override = os.environ.get("PYTEST")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("pytest")
    if on_path is not None:
        return on_path
    local = PROJECT_DIR / ".venv" / "bin" / "pytest"
    if local.exists():
        return str(local)
    sys.exit("pytest not found (set $PYTEST, put it on PATH, or install requirements-dev.txt into .venv)")


def main() -> int:
    pytest_bin = find_pytest()
    return subprocess.run([pytest_bin, "-q"], cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    sys.exit(main())
