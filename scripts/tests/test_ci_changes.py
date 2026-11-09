"""Exercise the workflow's Markdown shortcut against real Git changes."""

import os
import re
import subprocess
import textwrap
from pathlib import Path

import pytest

WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/ci.yml"


def git(repo: Path, *args: str, data: str | None = None) -> str:
    return subprocess.check_output(["git", *args], cwd=repo, input=data, text=True).strip()


@pytest.mark.parametrize(("names", "expected"), [
    (["README.md", "docs/notes.md"], "false"),
    (["README.md", "src/main.cpp"], "true"),
    ([f"src/{i:04d}_{'x' * 90}.cpp" for i in range(3000)], "true"),
])
def test_markdown_shortcut_reads_the_complete_diff(tmp_path: Path, names: list[str], expected: str) -> None:
    git(tmp_path, "init", "-q")
    git(tmp_path, "config", "user.name", "CI test")
    git(tmp_path, "config", "user.email", "ci@example.invalid")
    before = git(tmp_path, "commit-tree", git(tmp_path, "write-tree"), "-m", "before")
    blob = git(tmp_path, "hash-object", "-w", "--stdin", data="test\n")
    git(tmp_path, "update-index", "--index-info", data="".join(f"100644 {blob}\t{name}\n" for name in names))
    after = git(tmp_path, "commit-tree", git(tmp_path, "write-tree"), "-p", before, "-m", "after")
    git(tmp_path, "update-ref", "HEAD", after)
    git(tmp_path, "remote", "add", "origin", str(tmp_path))
    match = re.search(r'          firmware=true\n.*?(?=          echo "firmware=)',
                      WORKFLOW.read_text(encoding="utf-8"), re.DOTALL)
    assert match is not None
    script = textwrap.dedent(match[0]) + 'echo "$firmware"\n'
    result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c", script], cwd=tmp_path,
                            env={**os.environ, "BEFORE": before, "GITHUB_EVENT_NAME": "push"},
                            capture_output=True, text=True, check=True)
    assert result.stdout.strip() == expected
