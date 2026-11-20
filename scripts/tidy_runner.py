"""Run independent clang-tidy units with bounded concurrency and persistent diagnostic logs.

Each unit is one input and the whole command that analyses it. Dependency output verifies that a
successful process really parsed its input; diagnostics alone do not prove completion.
"""

import re
import subprocess
import time
from collections.abc import Callable, Sequence
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Unit:
    source: str
    command: tuple[str, ...]


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


def run_one(command: Sequence[str], source: str, prefix: Path, timeout: float) -> Result:
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
    # A unit clang could not parse whole still names the files it included, headers to analyse among them.
    dependencies: frozenset[str] = read_dependencies(dep) if status in (0, 1) and dep.exists() else frozenset()
    # Without dependency evidence a successful process has not proved that it parsed its input.
    if status == 0 and str(Path(source).resolve()) not in dependencies:
        status = 125
    return Result(source, status, time.monotonic() - started, log, dependencies)


def run_units(units: Sequence[Unit], directory: Path, jobs: int, report: Callable[[Result], None],
              timeout: float = 1200, first: int = 0) -> list[Result]:
    """Run every unit and report each as it completes; `first` numbers the logs on from an earlier batch."""
    directory.mkdir(parents=True, exist_ok=True)
    results: list[Result] = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        pending = [pool.submit(run_one, unit.command, unit.source, directory / f"unit-{first + i:04d}", timeout)
                   for i, unit in enumerate(units)]
        for future in as_completed(pending):
            result = future.result()
            results.append(result)
            report(result)
    return results


def failed(results: Sequence[Result]) -> bool:
    """Whether a tool crashed, timed out or produced no parse evidence; 1 is clang's parse error."""
    return any(result.status not in (0, 1) for result in results)
