"""Unit tests for scripts/release_tag.py: only a clean, pushed master head becomes v<commit count>."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import release_tag`
import release_tag


class Repository:
    """git answers from `answers`; a test changes an entry to break one precondition."""

    def __init__(self) -> None:
        self.answers = {"rev-parse --abbrev-ref HEAD": "master", "rev-parse HEAD": "abc",
                        "rev-parse origin/master": "abc", "rev-parse --short HEAD": "abc"}
        self.calls: list[str] = []

    def git(self, *args: str) -> str:
        command = " ".join(args)
        self.calls.append(command)
        return self.answers.get(command, "")


@pytest.fixture
def repository(monkeypatch: pytest.MonkeyPatch) -> Repository:
    fake = Repository()
    monkeypatch.setattr(release_tag, "git", fake.git)
    monkeypatch.setattr(release_tag, "get_git_dirty", lambda: 0)
    monkeypatch.setattr(release_tag, "get_git_commit_count", lambda: 42)
    return fake


def test_a_clean_pushed_master_is_tagged_with_its_commit_count(repository: Repository) -> None:
    assert release_tag.release_tag() == "v42"


@pytest.mark.parametrize(("command", "answer", "message"), [
    ("rev-parse --abbrev-ref HEAD", "feature/x", "only master"),
    ("rev-parse origin/master", "def", "differs from origin/master"),
    ("tag --list v42", "v42", "already exists"),
])
def test_anything_but_a_pushed_master_head_is_refused(
    repository: Repository, command: str, answer: str, message: str,
) -> None:
    repository.answers[command] = answer
    with pytest.raises(SystemExit, match=message):
        release_tag.release_tag()


@pytest.mark.usefixtures("repository")
def test_local_changes_are_refused(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(release_tag, "get_git_dirty", lambda: 1)
    with pytest.raises(SystemExit, match="changes"):
        release_tag.release_tag()


def test_a_dry_run_neither_tags_nor_pushes(repository: Repository, capsys: pytest.CaptureFixture[str]) -> None:
    assert release_tag.main(["--dry-run"]) == 0
    assert not any(call.startswith(("tag --annotate", "push")) for call in repository.calls)
    assert "v42" in capsys.readouterr().out


def test_a_release_tags_and_pushes(repository: Repository) -> None:
    assert release_tag.main([]) == 0
    assert repository.calls[-2:] == ["tag --annotate v42 --message CanSystems v42", "push origin v42"]
