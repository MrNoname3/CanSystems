"""Unit tests for scripts/deps_check.py: reading the requirement lines the gate checks.

Only the `-r` include belongs to this module - the requirement syntax itself is parsed by
`packaging` - so that is what these cover, together with the file-level details around it.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import deps_check`
import deps_check


def test_requirement_lines_are_read(tmp_path: Path) -> None:
    req = tmp_path / "requirements.txt"
    req.write_text("# a comment\nruff==0.16.5   # trailing note\n\npytest==9.1.1\n", encoding="utf-8")
    assert deps_check.read_requirements(req) == ["ruff==0.16.5", "pytest==9.1.1"]


def test_included_files_are_followed(tmp_path: Path) -> None:
    (tmp_path / "ota").mkdir()
    (tmp_path / "ota" / "runtime.txt").write_text("tqdm==4.68.3\n", encoding="utf-8")
    req = tmp_path / "requirements.txt"
    req.write_text("-r ota/runtime.txt\nruff==0.16.5\n", encoding="utf-8")
    assert deps_check.read_requirements(req) == ["tqdm==4.68.3", "ruff==0.16.5"]


def test_an_include_cycle_terminates(tmp_path: Path) -> None:
    a = tmp_path / "a.txt"
    b = tmp_path / "b.txt"
    a.write_text("-r b.txt\nruff==0.16.5\n", encoding="utf-8")
    b.write_text("-r a.txt\npytest==9.1.1\n", encoding="utf-8")
    assert deps_check.read_requirements(a) == ["pytest==9.1.1", "ruff==0.16.5"]


def test_a_missing_include_contributes_nothing(tmp_path: Path) -> None:
    req = tmp_path / "requirements.txt"
    req.write_text("-r nowhere.txt\nruff==0.16.5\n", encoding="utf-8")
    assert deps_check.read_requirements(req) == ["ruff==0.16.5"]


def test_the_venv_is_preferred_when_there_is_one(tmp_path: Path, monkeypatch: object) -> None:
    venv_python = tmp_path / "bin" / "python"
    venv_python.parent.mkdir(parents=True)
    venv_python.touch()
    monkeypatch.setattr(deps_check, "VENV_PYTHON", venv_python)   # type: ignore[attr-defined]
    assert deps_check.target_python() == venv_python


def test_without_a_venv_the_running_interpreter_is_checked(tmp_path: Path, monkeypatch: object) -> None:
    # CI has no project .venv: it installs the pins into the runner's own Python.
    monkeypatch.setattr(deps_check, "VENV_PYTHON", tmp_path / "absent" / "python")   # type: ignore[attr-defined]
    assert deps_check.target_python() == Path(sys.executable)


def test_the_projects_own_requirements_parse(tmp_path: Path) -> None:
    from packaging.requirements import Requirement
    lines = deps_check.read_requirements(deps_check.REQUIREMENTS)
    assert lines, "requirements-dev.txt yielded no requirements"
    names = {Requirement(line).name for line in lines}          # raises on anything unparseable
    assert {"ruff", "pytest", "packaging"} <= names
