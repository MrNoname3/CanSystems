"""Unit tests for scripts/toolchain_key.py: which platformio.ini changes give the CI toolchain image
a new tag."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import toolchain_key`
import toolchain_key

PROJECT_DIR = Path(__file__).resolve().parents[2]
INI = """\
[env]
framework = arduino
platform_packages = platformio/tool-cppcheck@1.0.0

[env:base]
platform = atmelavr@5.3.0
board = nano
build_flags = -D ONE

[env:node]
extends = env:base
platform_packages =
  ${env.platform_packages}
  platformio/tool-avrdude@1.0.0
lib_deps = some/lib@1.0.0
"""


def _config(tmp_path: Path, text: str) -> str:
    ini = tmp_path / "platformio.ini"
    ini.write_text(text, encoding="utf-8")
    return toolchain_key.toolchain_config(ini)


def test_this_project_resolves_every_reference_it_can() -> None:
    config = toolchain_key.toolchain_config(PROJECT_DIR / "platformio.ini")
    assert "platform = " in config
    assert "${env" not in config


def test_a_build_flag_or_a_library_leaves_the_tag_alone(tmp_path: Path) -> None:
    changed = INI.replace("-D ONE", "-D TWO").replace("some/lib@1.0.0", "other/lib@2.0.0")
    assert _config(tmp_path, changed) == _config(tmp_path, INI)


def test_a_platform_pin_changes_the_tag(tmp_path: Path) -> None:
    assert _config(tmp_path, INI.replace("atmelavr@5.3.0", "atmelavr@5.4.0")) != _config(tmp_path, INI)


def test_a_package_pinned_in_env_reaches_an_environment_that_replaces_the_list(tmp_path: Path) -> None:
    changed = INI.replace("tool-cppcheck@1.0.0", "tool-cppcheck@2.0.0")
    assert _config(tmp_path, changed) != _config(tmp_path, INI)
    assert "platformio/tool-cppcheck@2.0.0 platformio/tool-avrdude@1.0.0" in _config(tmp_path, changed)


def test_an_environment_on_a_platform_already_there_leaves_the_tag_alone(tmp_path: Path) -> None:
    added = INI + "\n[env:smoke]\nextends = env:node\nbuild_flags = -D SMOKE\n"
    assert _config(tmp_path, added) == _config(tmp_path, INI)


def test_an_option_moved_to_the_section_extended_leaves_the_tag_alone(tmp_path: Path) -> None:
    moved = INI.replace("board = nano\n", "").replace("[env:base]\n", "[env:base]\nboard = nano\n")
    inherited = INI.replace("board = nano\n", "").replace("[env]\n", "[env]\nboard = nano\n")
    assert _config(tmp_path, moved) == _config(tmp_path, INI)
    assert _config(tmp_path, inherited) == _config(tmp_path, INI)


def test_an_option_of_the_environment_itself_wins_over_one_it_extends(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text(INI + "board = uno\n", encoding="utf-8")
    parser = toolchain_key.read_config(ini)
    assert toolchain_key.resolve(parser, "env:node", "board") == "uno"
    assert toolchain_key.resolve(parser, "env:base", "board") == "nano"


def test_a_reference_platformio_ini_does_not_set_stays_as_written(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text("[env:a]\nplatform_packages = tool@${platformio.packages_dir}/tool\n", encoding="utf-8")
    parser = toolchain_key.read_config(ini)
    assert toolchain_key.resolve(parser, "env:a", "platform_packages") == "tool@${platformio.packages_dir}/tool"


def test_a_reference_cycle_ends_unexpanded(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text("[env:a]\nboard = ${env:b.board}\n\n[env:b]\nboard = ${env:a.board}\n", encoding="utf-8")
    assert "${env:" in (toolchain_key.resolve(toolchain_key.read_config(ini), "env:a", "board") or "")


def test_every_image_input_is_hashed(tmp_path: Path) -> None:
    for name in (*toolchain_key.IMAGE_INPUTS, "platformio.ini"):
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(INI if name == "platformio.ini" else name, encoding="utf-8")
    tag = toolchain_key.image_tag(tmp_path)
    for name in toolchain_key.IMAGE_INPUTS:
        path = tmp_path / name
        path.write_text(path.read_text(encoding="utf-8") + "\n", encoding="utf-8")
        assert toolchain_key.image_tag(tmp_path) != tag, name
        tag = toolchain_key.image_tag(tmp_path)
