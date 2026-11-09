"""Actual subprocess failures and preprocessor dependency coverage in the tidy runner."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import analysis_check
import build_jobs
import tidy_runner


def test_a_fractional_cpu_container_runs_one_analyser(tmp_path: Path) -> None:
    quota = tmp_path / "cpu.max"
    quota.write_text("150000 100000\n")
    assert build_jobs.analysis_jobs(quota) == 1


def test_memory_limits_the_worker_count(tmp_path: Path) -> None:
    quota = tmp_path / "cpu.max"
    quota.write_text("800000 100000\n")
    maximum, current = tmp_path / "memory.max", tmp_path / "memory.current"
    maximum.write_text(str(2 * 1024 ** 3))
    current.write_text(str(1024 ** 3))
    assert build_jobs.analysis_jobs(quota, maximum, current) == 1


def test_dependency_paths_handle_spaces_and_continuations(tmp_path: Path) -> None:
    dep = tmp_path / "unit.d"
    dep.write_text("tidy: /tmp/a\\ b.hpp \\\n /tmp/next.hpp\n")
    assert tidy_runner.read_dependencies(dep) == frozenset({"/tmp/a b.hpp", "/tmp/next.hpp"})


def test_a_process_that_crashes_after_output_fails(tmp_path: Path) -> None:
    result = tidy_runner.run_one([sys.executable, "-c", "import os, signal; print('note: before crash', flush=True); os.kill(os.getpid(), signal.SIGTERM)"],
                                 "input.cpp", tmp_path / "unit", 5)
    assert result.status == -15
    assert "before crash" in result.log.read_text()


def test_a_hung_process_is_terminated(tmp_path: Path) -> None:
    result = tidy_runner.run_one([sys.executable, "-c", "import time; time.sleep(30)"],
                                 "input.cpp", tmp_path / "unit", 0.1)
    assert result.status == 124


def test_a_successful_exit_without_parsing_fails(tmp_path: Path) -> None:
    result = tidy_runner.run_one([sys.executable, "-c", "pass"], "input.cpp", tmp_path / "unit", 5)
    assert result.status == 125


def test_no_project_file_is_excluded_from_every_platform() -> None:
    assert analysis_check.uncovered_inputs() == []


def test_headers_are_checked_independently_even_when_sources_include_them(tmp_path: Path, monkeypatch: object) -> None:
    source, header, orphan = (str(tmp_path / name) for name in ("main.cpp", "included.hpp", "unused.hpp"))
    seen: list[str] = []

    def run_one(command: list[str], path: str, prefix: Path, timeout: float) -> tidy_runner.Result:
        seen.append(path)
        return tidy_runner.Result(path, 0, 0.1, prefix, frozenset({source, header}))

    monkeypatch.setattr(tidy_runner, "run_one", run_one)   # type: ignore[attr-defined]
    status, count = tidy_runner.run_units(["clang-tidy", source, header, orphan, "--"],
                                         [source, header, orphan], tmp_path, 1, lambda result: None)
    assert (status, count) == (0, 3)
    assert seen == [source, header, orphan]


def test_a_parse_failure_keeps_header_checks(tmp_path: Path, monkeypatch: object) -> None:
    source, header = str(tmp_path / "main.cpp"), str(tmp_path / "included.hpp")
    seen: list[str] = []

    def run_one(command: list[str], path: str, prefix: Path, timeout: float) -> tidy_runner.Result:
        seen.append(path)
        return tidy_runner.Result(path, 1, 0.1, prefix, frozenset({header}))

    monkeypatch.setattr(tidy_runner, "run_one", run_one)   # type: ignore[attr-defined]
    assert tidy_runner.run_units(["clang-tidy", source, header, "--"], [source, header], tmp_path, 1,
                                 lambda result: None) == (1, 2)
    assert seen == [source, header]
