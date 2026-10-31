"""Unit tests for scripts/release_check.py: the steps of the whole gate and of each CI group.

Running the steps is the gate's own job; these cover which commands it would run.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import release_check`
import analysis_check
import release_check
from gate_groups import GROUPS

PIO = "pio"
DEPS = ["python", "deps_check.py"]
FAIL_ON = ["--fail-on-defect", "low", "--fail-on-defect", "medium", "--fail-on-defect", "high"]


def _steps(group: str | None) -> dict[str, list[str]]:
    selected = None if group is None else GROUPS[group]
    return {step.name: step.argv for step in release_check.gate_steps(selected, PIO, DEPS, [])}


def test_the_whole_gate_runs_every_step_over_every_environment() -> None:
    steps = _steps(None)
    assert list(steps) == ["deps", "build", "test", "check", "tidy", "format", "lint", "typecheck", "pytest"]
    assert steps["build"] == [PIO, "run"]
    assert steps["check"] == [PIO, "check", *FAIL_ON]
    assert steps["tidy"][-1].endswith("analysis_check.py")


def test_a_firmware_group_builds_and_cppchecks_its_environments() -> None:
    steps = _steps("avr")
    assert list(steps) == ["build", "check"]
    assert steps["build"] == [PIO, "run", "-e", "nanoatmega328_alert", "-e", "nanoatmega328_irrigation"]
    assert steps["check"] == [PIO, "check", "-e", "nanoatmega328_alert", "-e", "nanoatmega328_irrigation",
                              *FAIL_ON]


def test_a_tidy_group_runs_clang_tidy_over_its_environment() -> None:
    steps = _steps("tidy-esp32")
    assert list(steps) == ["tidy"]
    assert steps["tidy"][-2:] == ["--env", "check_esp32"]


def test_the_host_group_runs_the_native_tests_and_the_python_guards() -> None:
    steps = _steps("host")
    assert list(steps) == ["deps", "build", "test", "check", "format", "lint", "typecheck", "pytest"]
    assert steps["build"] == [PIO, "run", "-e", "native_test"]


def test_the_groups_together_run_what_the_whole_gate_runs() -> None:
    whole = _steps(None)
    built: list[str] = []
    checked: list[str] = []
    tidied: list[str] = []
    others: list[str] = []
    for group in GROUPS:
        for name, argv in _steps(group).items():
            named = [argv[i + 1] for i, arg in enumerate(argv) if arg == "-e"]
            if name == "build":
                built += named
            elif name == "check":
                checked += named
            elif name == "tidy":
                tidied += argv[argv.index("--env") + 1:]
            else:
                others.append(name)
    envs = release_check.default_envs(release_check.PROJECT_DIR / "platformio.ini")
    assert sorted(built) == sorted(envs)
    assert sorted(checked) == sorted(envs)
    assert sorted(tidied) == sorted(analysis_check.ENVIRONMENTS)
    assert sorted(others) == sorted(name for name in whole if name not in ("build", "check", "tidy"))


def test_the_build_takes_the_jobs_arguments() -> None:
    steps = {step.name: step.argv for step in release_check.gate_steps(GROUPS["esp32"], PIO, DEPS, ["--jobs", "3"])}
    assert steps["build"] == [PIO, "run", "-e", "project_esp32_can", "--jobs", "3"]
    assert "--jobs" not in steps["check"]
