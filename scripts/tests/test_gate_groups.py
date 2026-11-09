"""Unit tests for scripts/gate_groups.py: reading default_envs, and the coverage check that keeps
the CI groups doing what the whole release gate does."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import gate_groups`
import analysis_check
import gate_groups
from gate_groups import Group

PROJECT_DIR = Path(__file__).resolve().parents[2]
ENVS = ["alpha", "beta", "native_test"]
TIDY = ("check_alpha",)
GROUPS = {
    "host": Group(envs=("native_test",), host=True),
    "firmware": Group(envs=("alpha", "beta")),
    "tidy": Group(tidy=("check_alpha",)),
}


def test_the_groups_cover_this_project() -> None:
    envs = gate_groups.default_envs(PROJECT_DIR / "platformio.ini")
    assert envs, "no default_envs read from platformio.ini"
    assert gate_groups.coverage_gaps(envs, analysis_check.ENVIRONMENTS) == []


def test_groups_covering_everything_once_leave_no_gap() -> None:
    assert gate_groups.coverage_gaps(ENVS, TIDY, GROUPS) == []


def test_an_environment_in_no_group_is_named() -> None:
    assert gate_groups.coverage_gaps([*ENVS, "gamma"], TIDY, GROUPS) == [
        "default environment gamma is in no group"]


def test_an_environment_in_two_groups_is_named() -> None:
    groups = {**GROUPS, "other": Group(envs=("beta",))}
    assert gate_groups.coverage_gaps(ENVS, TIDY, groups) == ["default environment beta is in 2 groups"]


def test_a_group_environment_the_project_lacks_is_named() -> None:
    assert gate_groups.coverage_gaps(["alpha", "native_test"], TIDY, GROUPS) == [
        "default environment beta is in a group but not in the project"]


def test_a_check_environment_in_no_group_is_named() -> None:
    assert gate_groups.coverage_gaps(ENVS, (*TIDY, "check_beta"), GROUPS) == [
        "check_* environment check_beta is in no group"]


def test_the_host_steps_belong_to_one_group() -> None:
    groups = {**GROUPS, "tidy": Group(tidy=("check_alpha",), host=True)}
    assert gate_groups.coverage_gaps(ENVS, TIDY, groups) == [
        "2 groups run the native tests and the Python guards, not one"]


def test_default_envs_reads_lists_split_by_lines_or_commas(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text("[platformio]\ndefault_envs =\n  alpha, beta   ; the boards\n  native_test\n\n"
                   "[env:alpha]\nplatform = example\n", encoding="utf-8")
    assert gate_groups.default_envs(ini) == ENVS


def test_every_declared_check_environment_must_belong_to_a_group(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text("[env:check_alpha]\n[env:check_new]\n[env:ordinary]\n", encoding="utf-8")
    assert gate_groups.coverage_gaps(ENVS, gate_groups.check_envs(ini), GROUPS) == [
        "check_* environment check_new is in no group"]


def test_ci_matrix_follows_groups_including_new_ones() -> None:
    groups = {**GROUPS, "new": Group(tidy=("check_new",))}
    assert gate_groups.ci_matrix(groups) == {"include": [
        {"group": "firmware", "sizes": True}, {"group": "tidy", "sizes": False},
        {"group": "new", "sizes": False}]}
