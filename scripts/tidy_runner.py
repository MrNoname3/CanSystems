"""Run independent translation units with bounded concurrency and persistent diagnostic logs.

Headers retain their standalone passes: header diagnostics differ from those in an including
source's constructor context. Dependency output verifies that each successful process really
parsed its input; diagnostics alone do not prove completion.
"""

import re
import subprocess
import time
from collections.abc import Callable, Sequence
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Result:
    source: str
    status: int
    duration: float
    log: Path
    dependencies: frozenset[str]


def read_dependencies(path: Path) -> frozenset[str]:
    """Read clang's Make dependency output, including escaped spaces and continued lines."""
    text = path.read_text(encoding="utf-8").replace("\\\n", "")
    _, separator, body = text.partition(":")
    if not separator:
        raise ValueError(f"Missing dependency target in {path}")
    tokens = re.findall(r"(?:\\.|[^\s\\])+", body)
    return frozenset(str(Path(re.sub(r"\\(.)", r"\1", token).replace("$$", "$")).resolve()) for token in tokens)


def run_one(command: list[str], source: str, prefix: Path, timeout: float) -> Result:
    dep = prefix.with_suffix(".d")
    log = prefix.with_suffix(".log")
    dep.unlink(missing_ok=True)
    started = time.monotonic()
    # -Xclang= keeps LibTooling's argument adjuster from removing the dependency options.
    argv = [*command, "-Xclang=-dependency-file", f"-Xclang={dep}", "-Xclang=-MT", "-Xclang=tidy"]
    with log.open("w", encoding="utf-8") as output:
        try:
            result = subprocess.run(argv, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False)
            status = result.returncode
        except subprocess.TimeoutExpired:
            output.write(f"\nanalysis: timed out after {timeout:g}s\n")
            status = 124
    if status == 1:
        text = log.read_text(encoding="utf-8", errors="replace")
        if not re.search(r"^.+:\d+:\d+: error: .*\[clang-diagnostic-error\]$", text, re.MULTILINE):
            status = 2
    dependencies: frozenset[str] = read_dependencies(dep) if status == 0 and dep.exists() else frozenset()
    # Without dependency evidence a successful process has not proved that it parsed its input.
    if status == 0 and str(Path(source).resolve()) not in dependencies:
        status = 125
    return Result(source, status, time.monotonic() - started, log, dependencies)


def run_units(command: list[str], inputs: Sequence[str], directory: Path, jobs: int,
              report: Callable[[Result], None], timeout: float = 1200) -> tuple[int, int]:
    """Report every completed unit; fail if any tool crashes, times out or produces no parse evidence."""
    directory.mkdir(parents=True, exist_ok=True)
    separator = command.index("--")
    input_set = set(inputs)
    options = [arg for arg in command[1:separator] if arg not in input_set]
    flags = command[separator:]
    sources = [source for source in inputs if Path(source).suffix in (".c", ".cc", ".cpp", ".cxx", ".ino")]
    headers = [source for source in inputs if source not in sources]
    statuses: list[int] = []
    count = 0

    def batch(paths: list[str]) -> None:
        nonlocal count
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            pending = {pool.submit(run_one, [command[0], *options,
                                   "--extra-arg-before=-x" + ("c++-header" if source in headers else "c++"),
                                   source, *flags], source,
                                   directory / f"unit-{count + i:04d}", timeout): source
                       for i, source in enumerate(paths)}
            for future in as_completed(pending):
                result = future.result()
                statuses.append(result.status)
                count += 1
                report(result)

    batch(sources)
    batch(headers)
    failure = any(status not in (0, 1) for status in statuses)
    return (2 if failure or not count else max(statuses)), count
