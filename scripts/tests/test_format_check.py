"""Unit tests for scripts/format_check.py: the file list the guard works from, and the modes.

The guard asks git which files are tracked and then hands each name to clang-format or opens it
directly, so a name that comes back split into pieces turns into files that do not exist - the
batch run fails on them, and the per-file pass that follows reports them as drifted.

The mode check answers a failure that only shows up somewhere else: `core.fileMode` is off in this
repository, so a `chmod +x` never reaches a commit, and a script committed without the bit runs
nowhere but the machine it was written on. The tests set the modes through git for that reason,
never through the filesystem.

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


def _repo_with(tmp_path: Path, files: dict[str, str], executable: tuple[str, ...] = ()) -> Path:
    """A repository holding `files` (name -> content), with `executable` recorded 100755."""
    _git(tmp_path, "init")
    for name, content in files.items():
        (tmp_path / name).write_text(content, encoding="utf-8")
    _git(tmp_path, "add", "-A")
    for name in executable:
        _git(tmp_path, "update-index", "--chmod=+x", name)
    _git(tmp_path, "commit", "-m", "seed")
    return tmp_path


def test_a_shebang_file_git_has_not_recorded_executable_is_caught(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"tool.py": "#!/usr/bin/env python3\nprint(1)\n"})
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == (["tool.py"], [])


def test_a_shebang_file_recorded_executable_passes(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"tool.py": "#!/usr/bin/env python3\nprint(1)\n"}, executable=("tool.py",))
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == ([], [])


def test_a_file_recorded_executable_without_a_shebang_is_caught(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"notes.md": "# notes\n"}, executable=("notes.md",))
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == ([], ["notes.md"])


def test_an_ordinary_file_is_neither(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"notes.md": "# notes\n", "code.h": "int main() {}\n"})
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == ([], [])


def test_the_mode_is_read_from_git_and_not_from_the_filesystem(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # Exactly the case that got past a local run: executable on disk, 0644 in the commit.
    repo = _repo_with(tmp_path, {"tool.py": "#!/usr/bin/env python3\nprint(1)\n"})
    (repo / "tool.py").chmod(0o755)
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == (["tool.py"], [])


def test_a_tracked_file_that_is_not_checked_out_is_skipped(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"tool.py": "#!/usr/bin/env python3\nprint(1)\n"}, executable=("tool.py",))
    (repo / "tool.py").unlink()
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.check_recorded_modes() == ([], [])


def test_modes_come_back_with_whole_names(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo_with(tmp_path, {"with a space.py": "#!/bin/sh\n"}, executable=("with a space.py",))
    monkeypatch.setattr(format_check, "PROJECT_DIR", repo)
    assert format_check.git_ls_files_with_modes() == [("100755", "with a space.py")]
