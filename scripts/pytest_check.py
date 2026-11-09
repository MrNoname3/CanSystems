#!/usr/bin/env python3
"""Test guard for the release gate: run pytest over the Python unit tests.

Covers the OTA tool (ota/tests), the build helpers (scripts/tests) and the web installer
(web/tests); the authoritative list is `testpaths` in pyproject.toml.

pytest is required: if it is not found the gate fails (install requirements-dev.txt into
.venv, which also brings the OTA runtime deps the tests import). The tests themselves skip
when what they need is absent - the OTA deps (paho-mqtt, tqdm, pyyaml) via
pytest.importorskip in ota/tests, git via a skipif in scripts/tests - so an incomplete
environment still does not hard-fail mid-run.

The executable comes from the environment checked by deps_check.py: the project .venv
when present, otherwise the running interpreter's bin directory.
"""

import subprocess
import sys
from pathlib import Path

from tooling import find_tool

PROJECT_DIR = Path(__file__).resolve().parent.parent


def find_pytest() -> str:
    return find_tool("pytest")


def main() -> int:
    pytest_bin = find_pytest()
    return subprocess.run([pytest_bin, "-q"], cwd=PROJECT_DIR, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
