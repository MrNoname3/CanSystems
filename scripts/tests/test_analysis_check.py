"""Unit tests for scripts/analysis_check.py: how a compilation database becomes analyser commands,
which files count as covered, and which diagnostics fail the gate."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import analysis_check`
import analysis_check
from analysis_check import Entry
from tidy_runner import Result

PROJECT = analysis_check.PROJECT_DIR
GCC = "/opt/toolchain/bin/xtensa-esp32-elf-g++"


def _entry(*flags: str, compiler: str = GCC, file: str = "lib/a/src/a.cpp") -> Entry:
    path = str(PROJECT / file)
    return Entry(path, str(PROJECT), (compiler, "-o", "a.o", "-c", "-MMD", "-Os", "-mlongcalls",
                                      "-Wall", *flags, path))


def test_only_the_flags_that_shape_parsing_are_kept() -> None:
    entry = _entry("-DNAME=1", "-I", "lib/a/src", "-Iinclude", "-std=gnu++17", "-mmcu=atmega328p",
                   "-include", "pre.h", "-UOLD")
    assert analysis_check.compile_flags(entry) == ["-DNAME=1", "-I", "lib/a/src", "-Iinclude", "-std=gnu++17",
                                                    "-mmcu=atmega328p", "-include", "pre.h", "-UOLD"]


def test_the_standard_becomes_its_gnu_dialect() -> None:
    assert analysis_check.gnu_dialect("-std=c++17") == "-std=gnu++17"
    assert analysis_check.gnu_dialect("-std=c11") == "-std=gnu11"
    assert analysis_check.gnu_dialect("-std=gnu++17") == "-std=gnu++17"
    assert analysis_check.gnu_dialect("-DSTD=c++17") == "-DSTD=c++17"


def test_the_target_follows_the_compiler_name() -> None:
    assert analysis_check.clang_target("/x/bin/avr-g++") == "avr"
    assert analysis_check.clang_target("/x/bin/xtensa-lx106-elf-g++") == "xtensa"
    assert analysis_check.clang_target("/x/bin/xtensa-esp32-elf-g++") == "xtensa-esp-elf"
    assert analysis_check.clang_target("/x/bin/xtensa-esp-elf-g++") == "xtensa-esp-elf"
    assert analysis_check.clang_target("g++") is None


def test_a_tidy_command_carries_the_toolchain_and_the_entry_flags(monkeypatch: object) -> None:
    def toolchain(compiler: str, language: str, machine: tuple[str, ...]) -> tuple[tuple[str, ...], tuple[str, ...]]:
        assert (compiler, language, machine) == (GCC, "c++", ("-std=c++17",))
        return ("/opt/toolchain/include/c++",), ("-D__XTENSA__=1",)

    monkeypatch.setattr(analysis_check, "toolchain", toolchain)   # type: ignore[attr-defined]
    entry = _entry("-DNAME=1", "-std=c++17")
    command = analysis_check.tidy_command("clang-tidy", entry)
    separator = command.index("--")
    assert command[0] == "clang-tidy" and command[separator - 1] == entry.file
    flags = command[separator + 1:]
    assert flags[0] == "--target=xtensa-esp-elf"
    assert "-isystem/opt/toolchain/include/c++" in flags and "-D__XTENSA__=1" in flags
    assert flags[-2:] == ("-DNAME=1", "-std=gnu++17")
    assert "-mlongcalls" not in flags and "-Wall" not in flags
    header = analysis_check.tidy_command("clang-tidy", entry, header=True)
    assert "--extra-arg-before=-xc++-header" in header[:header.index("--")]


def test_cppcheck_keeps_the_project_include_directories_alone() -> None:
    entry = _entry("-DNAME=1", "-Ilib/a/src", f"-I{PROJECT}/.pio/libdeps/x/src", "-I/opt/framework/include",
                   "-isystem/opt/sys")
    arguments = analysis_check.cppcheck_arguments(entry)
    assert arguments == [GCC, "-DNAME=1", "-Ilib/a/src", "-c", entry.file]


def test_a_library_file_no_build_reaches_is_uncovered(tmp_path: Path, monkeypatch: object) -> None:
    monkeypatch.setattr(analysis_check, "PROJECT_DIR", tmp_path)   # type: ignore[attr-defined]
    files = {"src/main.cpp": '#include "used.hpp"\n', "lib/u/src/used.hpp": '#include <deep.hpp>\n',
             "lib/d/src/deep.hpp": "", "lib/o/src/orphan.cpp": "", "lib/o/src/orphan.hpp": ""}
    for name, text in files.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text)
    library = sorted(name for name in files if name.startswith("lib/"))
    assert analysis_check.uncovered({"src/main.cpp"}, library) == ["lib/o/src/orphan.cpp", "lib/o/src/orphan.hpp"]
    assert analysis_check.uncovered({"src/main.cpp", "lib/o/src/orphan.cpp"}, library) == ["lib/o/src/orphan.hpp"]


def test_every_library_file_counts() -> None:
    files = analysis_check.library_files()
    assert "lib/crc16/src/crc16.cpp" in files and "lib/crc16/src/crc16.hpp" in files


def _result(tmp_path: Path, *lines: str, status: int = 0) -> Result:
    log = tmp_path / "unit.log"
    log.write_text("\n".join(lines) + "\n")
    return Result("a.cpp", status, 0.1, log, frozenset())


def test_tidy_findings_split_project_defects_errors_and_the_rest(tmp_path: Path) -> None:
    own = PROJECT / "lib" / "a" / "src" / "a.cpp"
    result = _result(tmp_path,
                     f"{own}:3:5: warning: do not use 'else' after 'return' [readability-else-after-return]",
                     f"{own}:9:1: error: unknown type name 'Foo' [clang-diagnostic-error]",
                     f"{PROJECT}/.pio/libdeps/x/src/x.h:1:1: warning: something [readability-something]",
                     "/opt/framework/include/f.h:2:2: error: a framework error [clang-diagnostic-error]",
                     f"{own}:3:5: note: an explanation, not a finding")
    defects, errors, outside = analysis_check.tidy_findings([result, result])
    assert defects == ["lib/a/src/a.cpp:3: [warning] do not use 'else' after 'return' [readability-else-after-return]"]
    assert errors == ["lib/a/src/a.cpp:9: unknown type name 'Foo'"]
    assert outside == 4


def test_the_cppcheck_pin_is_read_from_platformio_ini(tmp_path: Path) -> None:
    ini = tmp_path / "platformio.ini"
    ini.write_text("[env]\nplatform_packages =\n  platformio/tool-cppcheck@1.21100.230717\n")
    assert analysis_check.pinned_cppcheck(ini) == "1.21100.230717"
    assert analysis_check.pinned_cppcheck() is not None


def test_an_installed_package_reports_its_version(tmp_path: Path) -> None:
    (tmp_path / "package.json").write_text('{"name": "tool-cppcheck", "version": "1.21100.230717"}')
    assert analysis_check.installed_version(tmp_path / "cppcheck") == "1.21100.230717"
    assert analysis_check.installed_version(tmp_path / "missing" / "cppcheck") is None


def test_the_test_suites_join_the_test_environment(tmp_path: Path) -> None:
    base = _entry("-DNATIVE_TEST", "-Itest/_shims/src", compiler="g++", file="src/main_native.cpp")
    entries = analysis_check.test_entries(base)
    names = {Path(entry.file).relative_to(PROJECT).as_posix() for entry in entries}
    assert "test/test_crc16/crc16_spec.cpp" in names
    crc = next(entry for entry in entries if entry.file.endswith("crc16_spec.cpp"))
    assert crc.arguments[0] == "g++" and "-DNATIVE_TEST" in crc.arguments
    assert f"-I{PROJECT / 'test' / 'test_crc16'}" in crc.arguments
    assert f"-I{PROJECT / 'lib' / 'crc16' / 'src'}" in crc.arguments
