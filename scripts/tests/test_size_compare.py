"""Unit tests for scripts/size_compare.py: the size files it writes and reads back, and which
builds it runs. The builds themselves are pio's; these replace them with fixed sizes."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import size_compare`
import size_compare
from size_compare import Sizes

CURRENT = {"project_esp32_can": Sizes(flash_used=1204780, flash_total=1703936, ram_used=53736, ram_total=327680)}
BASELINE = {"project_esp32_can": Sizes(flash_used=1204000, flash_total=1703936, ram_used=53700, ram_total=327680)}


class Builds:
    """Stands in for the two pio builds and records which ones ran, with which environments."""

    def __init__(self) -> None:
        self.baseline: list[list[str]] = []
        self.current: list[list[str]] = []

    def build_baseline(self, pio: str, envs: list[str], commit: str) -> dict[str, Sizes]:
        self.baseline.append(envs)
        return BASELINE

    def build_current(self, pio: str, envs: list[str], cwd: str | None = None) -> dict[str, Sizes]:
        self.current.append(envs)
        return CURRENT


def _run(monkeypatch: object, *args: str) -> Builds:
    builds = Builds()
    monkeypatch.setattr(size_compare, "_find_pio", lambda: "pio")   # type: ignore[attr-defined]
    monkeypatch.setattr(size_compare, "_build_baseline_in_worktree", builds.build_baseline)   # type: ignore[attr-defined]
    monkeypatch.setattr(size_compare, "_build_and_capture", builds.build_current)   # type: ignore[attr-defined]
    monkeypatch.setattr(sys, "argv", ["size_compare.py", "--commit", "abc1234", *args])   # type: ignore[attr-defined]
    size_compare.main()
    return builds


def test_saved_sizes_read_back_the_same(tmp_path: Path) -> None:
    path = tmp_path / "sizes.json"
    size_compare.save_sizes(path, CURRENT)
    assert size_compare.load_sizes(path) == CURRENT


def test_a_size_file_stands_in_for_the_baseline_build(monkeypatch: object, tmp_path: Path) -> None:
    path = tmp_path / "baseline.json"
    size_compare.save_sizes(path, BASELINE)
    builds = _run(monkeypatch, "--baseline-sizes", str(path))
    assert builds.baseline == []
    assert len(builds.current) == 1


def test_an_unusable_size_file_builds_the_baseline(monkeypatch: object, tmp_path: Path) -> None:
    path = tmp_path / "baseline.json"
    path.write_text('{"project_esp32_can": {"flash_used": 1}}\n', encoding="utf-8")
    builds = _run(monkeypatch, "--baseline-sizes", str(path))
    assert len(builds.baseline) == 1


def test_a_missing_size_file_builds_the_baseline(monkeypatch: object, tmp_path: Path) -> None:
    builds = _run(monkeypatch, "--baseline-sizes", str(tmp_path / "absent.json"))
    assert len(builds.baseline) == 1


def test_the_current_sizes_are_saved(monkeypatch: object, tmp_path: Path) -> None:
    path = tmp_path / "current.json"
    _run(monkeypatch, "--save-sizes", str(path))
    assert size_compare.load_sizes(path) == CURRENT


def test_a_group_builds_its_environments(monkeypatch: object) -> None:
    builds = _run(monkeypatch, "--group", "esp8266")
    assert builds.baseline == [["project_esp8266_rad", "project_esp8266_thermo"]]
    assert builds.current == [["project_esp8266_rad", "project_esp8266_thermo"]]
