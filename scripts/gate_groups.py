"""The parts the release gate splits into for CI, one job each.

A group names the default environments it builds and cppchecks, the check_* environments it
clang-tidies, and whether it runs the native tests and the Python guards. Together the groups do
what the whole gate does: coverage_gaps() lists what they would miss or do twice, and the gate
fails on it, so an environment added to platformio.ini cannot drop out of CI unnoticed.
"""

import configparser
import json
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Group:
    envs: tuple[str, ...] = ()      # default environments it builds and cppchecks
    tidy: tuple[str, ...] = ()      # check_* environments it runs clang-tidy over
    host: bool = False              # the native tests and the Python guards


GROUPS: dict[str, Group] = {
    "host": Group(envs=("native_test",), host=True),
    "avr": Group(envs=("nanoatmega328_alert", "nanoatmega328_irrigation")),
    "esp32": Group(envs=("project_esp32_can",)),
    "esp8266": Group(envs=("project_esp8266_rad", "project_esp8266_thermo")),
    "tidy-avr": Group(tidy=("check_avr",)),
    "tidy-esp32": Group(tidy=("check_esp32",)),
    "tidy-esp8266": Group(tidy=("check_esp8266",)),
}


def default_envs(ini: Path) -> list[str]:
    """The [platformio] default_envs of a platformio.ini, in order."""
    parser = configparser.ConfigParser(inline_comment_prefixes=(";",), interpolation=None, strict=False)
    parser.read(ini, encoding="utf-8")
    return [name for name in re.split(r"[\s,]+", parser.get("platformio", "default_envs", fallback=""))
            if name]


def coverage_gaps(envs: list[str], tidy: tuple[str, ...], groups: dict[str, Group] = GROUPS) -> list[str]:
    """What the groups do not cover exactly once: every default environment built, every check_*
    environment clang-tidied, and the host steps, each by one group alone."""
    gaps: list[str] = []
    for kind, wanted, covered in (
        ("default environment", envs, [env for group in groups.values() for env in group.envs]),
        ("check_* environment", list(tidy), [env for group in groups.values() for env in group.tidy]),
    ):
        gaps += [f"{kind} {env} is in no group" for env in wanted if env not in covered]
        gaps += [f"{kind} {env} is in {covered.count(env)} groups" for env in sorted(set(covered))
                 if covered.count(env) > 1]
        gaps += [f"{kind} {env} is in a group but not in the project" for env in sorted(set(covered))
                 if env not in wanted]
    hosts = [name for name, group in groups.items() if group.host]
    if len(hosts) != 1:
        gaps.append(f"{len(hosts)} groups run the native tests and the Python guards, not one")
    return gaps


def check_envs(ini: Path) -> tuple[str, ...]:
    """Every declared analysis environment, including ones not yet assigned to a group."""
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.read(ini, encoding="utf-8")
    return tuple(section.removeprefix("env:") for section in parser.sections() if section.startswith("env:check_"))


def ci_matrix(groups: dict[str, Group] = GROUPS) -> dict[str, list[dict[str, str | bool]]]:
    """Firmware/analysis jobs; the host group has its own workflow job."""
    return {"include": [{"group": name, "sizes": bool(group.envs)}
                        for name, group in groups.items() if not group.host]}


if __name__ == "__main__":
    print(json.dumps(ci_matrix(), separators=(",", ":")))
