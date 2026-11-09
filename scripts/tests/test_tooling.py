"""Guards must execute tools from the environment whose package pins were checked."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tooling


def test_path_cannot_override_the_pinned_tool(tmp_path: Path, monkeypatch: object) -> None:
    pinned = tmp_path / "venv" / "bin"
    foreign = tmp_path / "foreign"
    pinned.mkdir(parents=True)
    foreign.mkdir()
    for directory in (pinned, foreign):
        tool = directory / "ruff"
        tool.write_text("#!/bin/sh\nexit 0\n")
        tool.chmod(0o755)
    monkeypatch.setattr(tooling, "target_python", lambda: pinned / "python")   # type: ignore[attr-defined]
    monkeypatch.setenv("PATH", str(foreign))   # type: ignore[attr-defined]
    monkeypatch.delenv("RUFF", raising=False)   # type: ignore[attr-defined]
    assert tooling.find_tool("ruff") == str(pinned / "ruff")
    monkeypatch.setenv("RUFF", str(foreign / "ruff"))   # type: ignore[attr-defined]
    with pytest.raises(SystemExit, match="outside the pinned"):
        tooling.find_tool("ruff")
