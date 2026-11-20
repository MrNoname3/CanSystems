#!/usr/bin/env python3
"""Static analysis for the release gate: cppcheck or clang-tidy over each environment's own build.

`pio run -t compiledb` writes an environment's compilation database (scripts/compiledb_path.py
puts it in the build directory), and the analysers run on exactly the translation units it lists
that are the project's own, with the flags they are compiled with. A file is analysed by every
environment that builds it, and by no other: what a target never compiles is no defect of it. So
that no file drops out unseen, a library file no default environment builds fails the gate.

clang-tidy is the one requirements-dev.txt pins in the .venv, one version for every target. It
reads each GCC command translated for clang: the target named by the compiler, the toolchain's own
include directories and predefined macros asked from that compiler, and the GNU dialect of the
standard, GCC accepting its extensions under either. Every project header a unit includes is
analysed on its own as well, as its diagnostics differ from those in an including source.
tidy_runner.py fails a unit that crashed, timed out or parsed nothing, and a clang error in project
code fails the gate too, as clang-tidy skips whatever depends on what it could not parse.

cppcheck is the one platform_packages pins in platformio.ini. It is given the project's own
include directories alone, the frameworks' headers being no part of what it reports on, and the
test environment's database is extended by the test suites. It has to name every file it was
given, or the gate fails.

Findings count in project files only; one of any severity fails the gate.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from functools import cache
from pathlib import Path

from build_jobs import analysis_jobs
from gate_groups import default_envs
from tidy_runner import Result, Unit, failed, run_units

PROJECT_DIR = Path(__file__).resolve().parent.parent
INI = PROJECT_DIR / "platformio.ini"
TEST_ENVIRONMENT = "native_test"                     # the environment `pio test` runs the suites in
SOURCES = (".c", ".cc", ".cpp", ".cxx", ".ino")
HEADERS = (".h", ".hh", ".hpp", ".hxx", ".tpp")

# The target triple clang parses a toolchain's code for, by the compiler's name; a host compiler
# needs none. Espressif's GCC names the ESP32 xtensa-esp32-elf or xtensa-esp-elf.
TARGETS = (("avr-", "avr"), ("xtensa-lx106-elf-", "xtensa"),
           ("xtensa-esp32-elf-", "xtensa-esp-elf"), ("xtensa-esp-elf-", "xtensa-esp-elf"))

# -ferror-limit=0: the framework headers clang cannot parse would otherwise use up its 20 errors
# before the project code in a file is reached. GCC's predefined macros are passed as -D, and clang
# has its own, hence the two macro warnings off. GCC 15's <cmath> declares its float overloads
# constexpr around builtins clang cannot evaluate, which clang reports in every unit.
TIDY_FLAGS = ("-ferror-limit=0", "-Wno-macro-redefined", "-Wno-builtin-macro-redefined",
              "-Wno-unknown-attributes", "-Wno-register", "-Wno-format-security", "-Wno-invalid-constexpr")
# After GCC's macros: its __FLT32_DIG__ and __FLT64_DIG__ have libstdc++ declare limits for
# _Float32 and _Float64, which clang has no C++ support for.
TIDY_UNDEFINES = ("-U__FLT32_DIG__", "-U__FLT64_DIG__")

# cstyleCast only ever fires inside avr-libc's register macros (_SFR_MEM8 and the like).
CPPCHECK_FLAGS = ("--enable=warning,style,performance,portability", "--inline-suppr",
                  "--suppress=*:*.pio/*", "--suppress=cstyleCast", "--platform=unspecified")
CPPCHECK_REPORTED = ("error", "warning", "style", "performance", "portability")

TIDY_DIAGNOSTIC = re.compile(r"^(\S+?):(\d+):\d+: (warning|error): (.*) \[([\w.,-]+)\]$")
CPPCHECK_DIAGNOSTIC = re.compile(r"^(\S+?):(\d+):\d+: \[(\w+):(\w+)\] (.*)$")
CPPCHECK_CHECKING = re.compile(r"^Checking (\S+) \.\.\.")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


@dataclass(frozen=True)
class Entry:
    """One translation unit of a compilation database, its command split into arguments."""
    file: str
    directory: str
    arguments: tuple[str, ...]


def project_file(path: str) -> str | None:
    """The path relative to the project, if it is one of the project's own files."""
    try:
        relative = (PROJECT_DIR / path).resolve().relative_to(PROJECT_DIR.resolve())
    except ValueError:
        return None
    return None if relative.parts[0] == ".pio" else relative.as_posix()


def find_pio() -> str:
    """Locate the PlatformIO CLI: PATH first, then the standard penv location."""
    pio = shutil.which("pio")
    if pio is not None:
        return pio
    fallback = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if fallback.exists():
        return str(fallback)
    sys.exit("pio executable not found (PATH and ~/.platformio/penv/bin/pio checked)")


def load_database(path: Path) -> list[Entry]:
    """The entries of a compilation database, each command split into its arguments."""
    entries: list[Entry] = []
    for item in json.loads(path.read_text(encoding="utf-8")):
        arguments = item.get("arguments") or shlex.split(item["command"])
        file = str((Path(item["directory"]) / item["file"]).resolve())
        entries.append(Entry(file, item["directory"], tuple(arguments)))
    return entries


def compile_database(pio: str, environment: str) -> list[Entry]:
    """The environment's compilation database, written fresh by PlatformIO."""
    result = subprocess.run([pio, "run", "-e", environment, "-t", "compiledb"], cwd=PROJECT_DIR,
                            capture_output=True, text=True, check=False,
                            env={**os.environ, "VIRTUAL_ENV": ""})
    path = PROJECT_DIR / ".pio" / "build" / environment / "compile_commands.json"
    if result.returncode != 0 or not path.exists():
        raise RuntimeError(f"{environment}: `pio run -t compiledb` failed:\n{result.stdout}{result.stderr}")
    return load_database(path)


def own_entries(entries: Iterable[Entry]) -> list[Entry]:
    """The entries that compile one of the project's own files."""
    return [entry for entry in entries if project_file(entry.file) is not None]


def compile_flags(entry: Entry) -> list[str]:
    """The arguments that shape how a file parses: defines, include paths, standard, MCU."""
    flags: list[str] = []
    arguments = iter(entry.arguments[1:])
    for argument in arguments:
        if argument in ("-include", "-isystem", "-iquote", "-I", "-D", "-U"):
            flags += [argument, next(arguments, "")]
        elif argument.startswith(("-D", "-U", "-I", "-isystem", "-iquote", "-include", "-std=", "-mmcu=")):
            flags.append(argument)
    return flags


def gnu_dialect(flag: str) -> str:
    """-std=c++NN as -std=gnu++NN: GCC accepts its extensions in either, clang in the GNU one only."""
    return re.sub(r"^-std=c(\+\+)?", lambda match: "-std=gnu" + (match.group(1) or ""), flag)


def clang_target(compiler: str) -> str | None:
    """The target triple for clang, from the toolchain's compiler name; None for the host."""
    name = Path(compiler).name
    return next((triple for prefix, triple in TARGETS if name.startswith(prefix)), None)


@cache
def toolchain(compiler: str, language: str, machine: tuple[str, ...]) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """The compiler's own include directories, in its search order, and its predefined macros."""
    result = subprocess.run([compiler, *machine, "-x", language, "-E", "-v", "-dM", os.devnull],
                            capture_output=True, text=True, check=True)
    search = re.search(r"#include <\.\.\.> search starts here:\n(.*?)End of search list", result.stderr, re.S)
    directories = tuple(str(Path(line.strip()).resolve()) for line in search.group(1).splitlines()) if search else ()
    macros = tuple(f"-D{match.group(1)}" + (f"={match.group(2)}" if match.group(2) else "")
                   for match in re.finditer(r"^#define (\S+) ?(.*)$", result.stdout, re.MULTILINE))
    return directories, macros


def tidy_command(binary: str, entry: Entry, header: bool = False) -> tuple[str, ...]:
    """The clang-tidy command that analyses the entry's file with its compile flags."""
    flags = compile_flags(entry)
    cplusplus = header or Path(entry.file).suffix != ".c"
    machine = tuple(flag for flag in flags if flag.startswith(("-std=", "-mmcu=")))
    directories, macros = toolchain(entry.arguments[0], "c++" if cplusplus else "c", machine)
    target = clang_target(entry.arguments[0])
    options = ["--quiet", f"--config-file={PROJECT_DIR / '.clang-tidy'}"]
    if header:
        options.append("--extra-arg-before=-xc++-header")
    return (binary, *options, entry.file, "--",
            *([f"--target={target}"] if target else []),
            *TIDY_FLAGS, "-nostdinc", *(["-nostdinc++"] if cplusplus else []),
            *(f"-isystem{directory}" for directory in directories), *macros, *TIDY_UNDEFINES,
            *(gnu_dialect(flag) for flag in flags))


def library_files() -> list[str]:
    """Every C and C++ file of the project's libraries."""
    return sorted(path.relative_to(PROJECT_DIR).as_posix() for path in (PROJECT_DIR / "lib").rglob("*")
                  if path.suffix in SOURCES + HEADERS)


def uncovered(built: Iterable[str], files: Sequence[str] | None = None) -> list[str]:
    """The library files no build reaches: neither compiled, nor named in an #include of one that is."""
    files = library_files() if files is None else files
    by_name: dict[str, list[str]] = {}
    for file in files:
        by_name.setdefault(Path(file).name, []).append(file)
    reached = set(built)
    pending = list(reached)
    while pending:
        text = (PROJECT_DIR / pending.pop()).read_text(encoding="utf-8", errors="replace")
        for name in INCLUDE.findall(text):
            for file in by_name.get(Path(name).name, []):
                if file not in reached:
                    reached.add(file)
                    pending.append(file)
    return [file for file in files if file not in reached]


def tidy_findings(results: Iterable[Result]) -> tuple[list[str], list[str], int]:
    """Project defects and clang errors in project files, and how many diagnostics fell outside."""
    defects: set[str] = set()
    errors: set[str] = set()
    outside = 0
    for result in results:
        for line in result.log.read_text(encoding="utf-8", errors="replace").splitlines():
            match = TIDY_DIAGNOSTIC.match(line)
            if match is None:
                continue
            path = project_file(match.group(1))
            if path is None:
                outside += 1
            elif match.group(5) == "clang-diagnostic-error":
                errors.add(f"{path}:{match.group(2)}: {match.group(4)}")
            else:
                defects.add(f"{path}:{match.group(2)}: [{match.group(3)}] {match.group(4)} [{match.group(5)}]")
    return sorted(defects), sorted(errors), outside


def run_tidy(environment: str, entries: Sequence[Entry], binary: str, jobs: int) -> int:
    """clang-tidy over the environment's own sources, then over every project header they include."""
    directory = PROJECT_DIR / ".pio" / "analysis" / environment
    units = [Unit(entry.file, tidy_command(binary, entry)) for entry in entries]
    print(f"analysis: {environment}: {len(units)} sources, {jobs} worker(s)", flush=True)

    def report(result: Result) -> None:
        print(f"analysis: {environment} {project_file(result.source)} {result.duration:.1f}s "
              f"exit={result.status}", flush=True)

    results = run_units(units, directory, jobs, report)
    including: dict[str, Entry] = {}
    by_source = {entry.file: entry for entry in entries}
    for result in sorted(results, key=lambda item: item.source):
        for dependency in sorted(result.dependencies):
            if Path(dependency).suffix in HEADERS and project_file(dependency) is not None:
                including.setdefault(dependency, by_source[result.source])
    headers = [Unit(header, tidy_command(binary, Entry(header, entry.directory, entry.arguments), header=True))
               for header, entry in sorted(including.items())]
    print(f"analysis: {environment}: {len(headers)} headers", flush=True)
    results += run_units(headers, directory, jobs, report, first=len(units))

    defects, errors, outside = tidy_findings(results)
    if outside:
        print(f"analysis: {environment}: {outside} diagnostic(s) in library and framework code left out")
    if failed(results) or not units:
        broken = [f"{project_file(r.source)} exit={r.status} ({r.log})" for r in results if r.status not in (0, 1)]
        print(f"analysis: {environment}: clang-tidy did not complete: {', '.join(broken) or 'no sources'}")
        return 1
    if errors:
        print(f"\nanalysis: {environment}: clang could not parse project code, so clang-tidy skipped what "
              "depends on it:")
        for error in errors:
            print(f"  {error}")
    if defects:
        print(f"\nanalysis: {environment}: {len(defects)} defect(s) in project code:")
        for defect in defects:
            print(f"  {defect}")
    return 1 if errors or defects else 0


def cppcheck_binary() -> Path:
    """The pinned cppcheck PlatformIO installed for platform_packages."""
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR") or Path.home() / ".platformio")
    return core / "packages" / "tool-cppcheck" / "cppcheck"


def pinned_cppcheck(ini: Path = INI) -> str | None:
    """The tool-cppcheck version platformio.ini pins."""
    match = re.search(r"tool-cppcheck@(\S+)", ini.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def installed_version(binary: Path) -> str | None:
    """The version of the package a tool binary belongs to."""
    try:
        return json.loads((binary.parent / "package.json").read_text(encoding="utf-8")).get("version")
    except (OSError, ValueError):
        return None


def cppcheck_arguments(entry: Entry, extra_includes: Sequence[str] = ()) -> list[str]:
    """The entry's command with only the project's own include directories kept."""
    kept: list[str] = [entry.arguments[0]]
    for flag in compile_flags(entry):
        if flag.startswith("-I"):
            directory = flag[2:]
            if project_file(str(Path(entry.directory) / directory)) is None:
                continue
        if flag.startswith(("-isystem", "-include")):
            continue
        kept.append(flag)
    return [*kept, *(f"-I{directory}" for directory in extra_includes), "-c", entry.file]


def test_entries(base: Entry) -> list[Entry]:
    """The test suites and their shims, compiled the way the test environment compiles its main."""
    libraries = sorted(str(path) for path in (PROJECT_DIR / "lib").glob("*/src") if path.is_dir())
    tests = sorted(path for path in (PROJECT_DIR / "test").rglob("*") if path.suffix in SOURCES)
    shims = str(PROJECT_DIR / "test" / "_shims" / "src")
    return [Entry(str(path), base.directory,
                  tuple(cppcheck_arguments(base, [str(path.parent), shims, *libraries])[:-2]))
            for path in tests]


def run_cppcheck(environment: str, entries: Sequence[Entry], jobs: int) -> int:
    """cppcheck over the environment's own sources, from a database of them alone."""
    binary = cppcheck_binary()
    pinned = pinned_cppcheck()
    installed = installed_version(binary)
    if installed is None or installed != pinned:
        print(f"analysis: {environment}: cppcheck {installed} at {binary} is not the pinned {pinned}")
        return 1
    database = [Entry(entry.file, entry.directory, tuple(cppcheck_arguments(entry))) for entry in entries]
    if environment == TEST_ENVIRONMENT and entries:
        database += [Entry(entry.file, entry.directory, (*entry.arguments, "-c", entry.file))
                     for entry in test_entries(entries[0])]
    directory = PROJECT_DIR / ".pio" / "analysis" / environment
    directory.mkdir(parents=True, exist_ok=True)
    project = directory / "cppcheck.json"
    project.write_text(json.dumps([{"directory": entry.directory, "file": entry.file,
                                    "arguments": list(entry.arguments)} for entry in database], indent=1),
                       encoding="utf-8")
    print(f"analysis: {environment}: cppcheck {installed}, {len(database)} sources", flush=True)
    result = subprocess.run([str(binary), f"--project={project}", *CPPCHECK_FLAGS, f"-j{jobs}",
                             "--template={file}:{line}:{column}: [{severity}:{id}] {message}"],
                            cwd=PROJECT_DIR, capture_output=True, text=True, check=False)
    checked = {str(Path(match.group(1)).resolve()) for line in result.stdout.splitlines()
               for match in [CPPCHECK_CHECKING.match(line)] if match is not None}
    missing = [project_file(entry.file) or entry.file for entry in database
               if str(Path(entry.file).resolve()) not in checked]
    defects = sorted({f"{path}:{match.group(2)}: [{match.group(3)}:{match.group(4)}] {match.group(5)}"
                      for line in result.stderr.splitlines()
                      for match in [CPPCHECK_DIAGNOSTIC.match(line)] if match is not None
                      for path in [project_file(match.group(1))]
                      if path is not None and match.group(3) in CPPCHECK_REPORTED})
    (directory / "cppcheck.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != 0 or missing or not database:
        print(f"analysis: {environment}: cppcheck did not complete (exit {result.returncode}); "
              f"not checked: {', '.join(missing) or 'none'}\n{result.stderr[-2000:]}")
        return 1
    if defects:
        print(f"\nanalysis: {environment}: {len(defects)} defect(s) in project code:")
        for defect in defects:
            print(f"  {defect}")
        return 1
    return 0


def main(argv: Sequence[str] = ()) -> int:
    environments_all = default_envs(INI)
    parser = argparse.ArgumentParser(description="cppcheck or clang-tidy over each environment's own build.")
    parser.add_argument("--tool", choices=("cppcheck", "clang-tidy"), required=True)
    parser.add_argument("--env", nargs="+", choices=environments_all, default=environments_all,
                        metavar="ENV", help=f"environments to analyse (default: {', '.join(environments_all)})")
    args = parser.parse_args(list(argv))
    environments: list[str] = args.env

    pio = find_pio()
    databases = {environment: own_entries(compile_database(pio, environment)) for environment in environments_all}
    missed = uncovered({path for entries in databases.values() for entry in entries
                        for path in [project_file(entry.file)] if path is not None})
    if missed:
        print("analysis: library files no default environment builds, so no analyser sees them:")
        for file in missed:
            print(f"  {file}")
        return 1

    jobs = analysis_jobs()
    status = 0
    for environment in environments:
        if args.tool == "cppcheck":
            status |= run_cppcheck(environment, databases[environment], jobs)
        else:
            binary = shutil.which("clang-tidy", path=str(Path(sys.executable).parent)) or ""
            if not binary:
                print(f"analysis: no clang-tidy beside {sys.executable}; install requirements-dev.txt")
                return 1
            status |= run_tidy(environment, databases[environment], binary, jobs)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
