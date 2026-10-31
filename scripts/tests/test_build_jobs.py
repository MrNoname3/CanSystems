"""Unit tests for scripts/build_jobs.py: the --jobs a cgroup's CPU quota gives `pio run`."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import build_jobs`
import build_jobs


def _cpu_max(tmp_path: Path, text: str) -> Path:
    path = tmp_path / "cpu.max"
    path.write_text(text, encoding="ascii")
    return path


def test_a_whole_cpu_quota_gives_as_many_jobs(tmp_path: Path) -> None:
    assert build_jobs.jobs_args(_cpu_max(tmp_path, "300000 100000\n")) == ["--jobs", "3"]


def test_a_part_cpu_is_rounded_up(tmp_path: Path) -> None:
    assert build_jobs.quota_jobs(_cpu_max(tmp_path, "150000 100000\n")) == 2
    assert build_jobs.quota_jobs(_cpu_max(tmp_path, "10000 100000\n")) == 1


def test_no_quota_keeps_the_default(tmp_path: Path) -> None:
    assert build_jobs.jobs_args(_cpu_max(tmp_path, "max 100000\n")) == []


def test_no_cgroup_file_keeps_the_default(tmp_path: Path) -> None:
    assert build_jobs.jobs_args(tmp_path / "absent") == []


def test_an_unreadable_quota_keeps_the_default(tmp_path: Path) -> None:
    assert build_jobs.jobs_args(_cpu_max(tmp_path, "garbage\n")) == []
