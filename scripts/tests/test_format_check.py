"""Unit tests for scripts/format_check.py: the file list the format guard works from.

The guard asks git which files are tracked and then hands each name to clang-format or opens it
directly, so a name that comes back split into pieces turns into files that do not exist - the
batch run fails on them, and the per-file pass that follows reports them as drifted.

Each test runs against a throwaway repository under tmp_path, with the guard pointed at it.
"""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import format_check`
import format_check

pytestmark = pytest.mark.skipif(shutil.which("git") is None, reason="git is not installed")


def _git(repo: Path, *args: str) -> None:
    """Runs one git command in `repo`, isolated from the user's global git configuration."""
    subprocess.run(
        [
            "git",
            "-c", "user.name=Test",
            "-c", "user.email=test@example.com",
            "-c", "commit.gpgsign=false",
            "-c", "init.defaultBranch=main",
            *args,
        ],
        cwd=repo, check=True, capture_output=True,
    )


def _repo_tracking(tmp_path: Path, *names: str) -> Path:
    """A repository with the named files committed."""
    _git(tmp_path, "init")
    for name in names:
        (tmp_path / name).write_text("int main() {}\n", encoding="utf-8")
    _git(tmp_path, "add", "-A")
    _git(tmp_path, "commit", "-m", "seed")
    return tmp_path


def test_tracked_names_are_listed_whole(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_tracking(tmp_path, "plain.h", "with a space.h")
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.git_ls_files() == ["plain.h", "with a space.h"]


def test_a_pathspec_still_selects(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_tracking(tmp_path, "kept.h", "skipped.txt")
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.git_ls_files("*.h") == ["kept.h"]
