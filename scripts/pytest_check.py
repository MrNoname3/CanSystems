#!/usr/bin/env python3
"""Test guard for the release gate: run pytest over the Python unit tests (ota/tests).

Skips with a note and exits 0 if pytest is not installed, so the local gate stays usable;
CI installs pytest + the OTA runtime deps and enforces it. The tests themselves skip when
the OTA deps (paho-mqtt, tqdm, pyyaml) are absent (pytest.importorskip in ota/tests), so a
deps-less environment never hard-fails here either.

pytest lookup order: $PYTEST, then `pytest` on PATH, then a project-local .venv.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent


def find_pytest() -> str | None:
    override = os.environ.get("PYTEST")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("pytest")
    if on_path is not None:
        return on_path
    local = PROJECT_DIR / ".venv" / "bin" / "pytest"
    return str(local) if local.exists() else None


def main() -> int:
    pytest_bin = find_pytest()
    if pytest_bin is None:
        print("pytest: not found - skipping (install pytest, or let CI enforce it)")
        return 0
    return subprocess.run([pytest_bin, "-q"], cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    sys.exit(main())
