#!/usr/bin/env python3
"""Workflow guard for the release gate: actionlint and zizmor over the CI configuration.

actionlint checks the workflow files against the Actions schema and their expressions, and runs
shellcheck over every `run:` script. zizmor audits the workflows and the composite actions for
security mistakes (configured in .github/zizmor.yml), offline: the audits that would ask GitHub's
API about the actions referenced stay out. All three tools are required, from requirements-dev.txt.

Lookup order for each: $ACTIONLINT / $ZIZMOR / $SHELLCHECK, then PATH, then the project-root .venv.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent


def find_tool(name: str) -> str:
    override = os.environ.get(name.upper())
    if override and Path(override).exists():
        return override
    on_path = shutil.which(name)
    if on_path is not None:
        return on_path
    local = PROJECT_DIR / ".venv" / "bin" / name
    if local.exists():
        return str(local)
    sys.exit(f"{name} not found (set ${name.upper()}, put it on PATH, or install requirements-dev.txt into .venv)")


def version(tool: str) -> str:
    output = subprocess.run([tool, "--version"], capture_output=True, text=True, check=False).stdout
    return output.splitlines()[0] if output else "?"


def main() -> int:
    actionlint, zizmor, shellcheck = find_tool("actionlint"), find_tool("zizmor"), find_tool("shellcheck")
    print(f"workflows: actionlint {version(actionlint)}, {version(zizmor)}", flush=True)
    # Both run whatever the first finds, so one pass reports everything.
    lint = subprocess.run([actionlint, "-shellcheck", shellcheck], cwd=PROJECT_DIR, check=False).returncode
    audit = subprocess.run([zizmor, "--offline", "--no-progress", ".github"], cwd=PROJECT_DIR, check=False).returncode
    return 1 if lint or audit else 0


if __name__ == "__main__":
    sys.exit(main())
