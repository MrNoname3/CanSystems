"""Actual subprocess failures and preprocessor dependency coverage in the tidy runner."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
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


def test_every_unit_runs_and_is_reported(tmp_path: Path, monkeypatch: object) -> None:
    seen: list[str] = []

    def run_one(command: tuple[str, ...], path: str, prefix: Path, timeout: float) -> tidy_runner.Result:
        assert command == ("clang-tidy", path)
        return tidy_runner.Result(path, 0, 0.1, prefix, frozenset({path}))

    monkeypatch.setattr(tidy_runner, "run_one", run_one)   # type: ignore[attr-defined]
    units = [tidy_runner.Unit(name, ("clang-tidy", name)) for name in ("a.cpp", "b.hpp")]
    results = tidy_runner.run_units(units, tmp_path, 1, lambda result: seen.append(result.source))
    assert sorted(seen) == ["a.cpp", "b.hpp"]
    assert not tidy_runner.failed(results)


def test_a_parse_error_is_no_tool_failure_but_a_crash_is() -> None:
    def result(status: int) -> tidy_runner.Result:
        return tidy_runner.Result("a.cpp", status, 0.1, Path("a.log"), frozenset())

    assert not tidy_runner.failed([result(0), result(1)])
    assert tidy_runner.failed([result(0), result(2)])
    assert tidy_runner.failed([result(124)])
