"""Unit tests for scripts/git_utils.py: the working-tree state the build reads.

get_git_dirty() feeds the firmware's GIT_DIRTY build flag (published in the info topic) and
step 0 of the release gate, so "dirty" has to cover every uncommitted change - staged ones
included, since staging is exactly what happens right before a commit.

Each test runs against a throwaway repository under tmp_path: the helpers inspect the process
working directory rather than taking a path, so the tests chdir into it.
"""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import git_utils`
import git_utils

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


def _repo(tmp_path: Path) -> Path:
    """A repository holding one committed file, so HEAD exists and the tree starts clean."""
    _git(tmp_path, "init", "-q")
    (tmp_path / "tracked.txt").write_text("original\n", encoding="utf-8")
    _git(tmp_path, "add", "tracked.txt")
    _git(tmp_path, "commit", "-q", "-m", "initial")
    return tmp_path


def test_clean_tree_is_not_dirty(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    assert git_utils.get_git_dirty() == 0


def test_unstaged_modification_is_dirty(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    (repo / "tracked.txt").write_text("modified\n", encoding="utf-8")
    assert git_utils.get_git_dirty() == 1


def test_untracked_file_is_dirty(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    (repo / "untracked.txt").write_text("new\n", encoding="utf-8")
    assert git_utils.get_git_dirty() == 1


def test_staged_modification_is_dirty(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    (repo / "tracked.txt").write_text("modified\n", encoding="utf-8")
    _git(repo, "add", "tracked.txt")
    # Staging makes the index match the worktree again, so an index-vs-worktree diff reports
    # nothing - the change is still uncommitted and the firmware must not claim to be clean.
    assert git_utils.get_git_dirty() == 1


def test_staged_new_file_is_dirty(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    (repo / "added.txt").write_text("new\n", encoding="utf-8")
    _git(repo, "add", "added.txt")
    # Staging also removes the file from the untracked list, so neither of the two checks
    # git_utils runs would see it unless the diff is taken against HEAD.
    assert git_utils.get_git_dirty() == 1


def test_hash_and_count_are_read_from_the_repository(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    short = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=repo, check=True, capture_output=True, text=True,
    ).stdout.strip()
    assert git_utils.get_git_hash() == int(short, 16)
    assert git_utils.get_git_commit_count() == 1


def test_missing_git_falls_back_to_zeros(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    monkeypatch.setenv("PATH", "")  # git is no longer resolvable -> FileNotFoundError
    assert git_utils.get_git_hash() == 0
    assert git_utils.get_git_commit_count() == 0
    # Nothing can be verified, so the tree counts as dirty rather than silently clean.
    assert git_utils.get_git_dirty() == 1


def test_outside_a_repository_falls_back_to_zeros(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(tmp_path)  # never initialised as a repository
    assert git_utils.get_git_hash() == 0
    assert git_utils.get_git_commit_count() == 0
    assert git_utils.get_git_dirty() == 1


def test_committing_returns_to_clean(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    repo = _repo(tmp_path)
    monkeypatch.chdir(repo)
    (repo / "tracked.txt").write_text("modified\n", encoding="utf-8")
    _git(repo, "add", "tracked.txt")
    _git(repo, "commit", "-q", "-m", "second")
    assert git_utils.get_git_dirty() == 0
