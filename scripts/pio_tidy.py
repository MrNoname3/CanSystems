"""PlatformIO adapter, run with its own interpreter, retaining its compiler configuration.

Only the clang-tidy driver is replaced, for this process alone. PlatformIO owns package setup,
include ordering, predefined macros and defect formatting; the adapter owns tool completion.
"""

import sys
from pathlib import Path

import platformio.check.tools
from analysis_check import (
    CLANG_TIDY_VERSION,
    PROJECT_DIR,
    clang_tidy_version,
    excluded_inputs,
    stale_exclusions,
    target_inputs,
    validate_inputs,
)
from build_jobs import analysis_jobs
from platformio.__main__ import main
from platformio.check.tools.clangtidy import ClangtidyCheckTool
from tidy_runner import run_units

# One cheap check, as clang-tidy refuses to run none: the left-out files are only parsed.
PARSE_ONLY = "--checks=-*,misc-unused-alias-decls"


class CheckedClangTidy(ClangtidyCheckTool):
    @staticmethod
    def is_check_successful(result):
        return result["returncode"] in (0, 1)

    def execute_check_cmd(self, cmd):
        if clang_tidy_version(cmd[0]) != CLANG_TIDY_VERSION:
            raise RuntimeError(f"Expected clang-tidy {CLANG_TIDY_VERSION}: {cmd[0]}")
        separator = cmd.index("--")
        inputs = [arg for arg in cmd[1:separator] if not arg.startswith("-") and Path(arg).is_file()]
        validate_inputs(self.envname, inputs)
        selected = target_inputs(self.envname, inputs)
        if not selected:
            raise RuntimeError("clang-tidy has no input files")
        options = [arg for arg in cmd[1:separator] if arg not in inputs]
        command = [cmd[0], *options, *selected, *cmd[separator:]]
        jobs = analysis_jobs()
        print(f"analysis: {self.envname}: {len(selected)} inputs, {jobs} worker(s)", flush=True)
        completed = 0

        def report(result):
            nonlocal completed
            completed += 1
            with result.log.open(encoding="utf-8", errors="replace") as log:
                for line in log:
                    self.on_tool_output(line.rstrip("\n"))
            print(f"analysis: {self.envname} [{completed}/{len(selected)}] "
                  f"{Path(result.source).relative_to(PROJECT_DIR)} {result.duration:.1f}s exit={result.status}", flush=True)

        status, count = run_units(command, selected, PROJECT_DIR / ".pio" / "analysis" / self.envname,
                                  jobs, report)

        excluded = excluded_inputs(self.envname, inputs)
        left_out = sorted({source for sources in excluded.values() for source in sources})
        unparsed: set[str] = set()

        def probe(result):
            if result.status == 1:
                unparsed.add(result.source)
            print(f"analysis: {self.envname} left out {Path(result.source).relative_to(PROJECT_DIR)} "
                  f"{'does not parse' if result.status == 1 else f'exit={result.status}'}", flush=True)

        if left_out:
            # Their logs stay out of on_tool_output: these errors are the expected ones.
            probed, _ = run_units([cmd[0], *options, PARSE_ONLY, *left_out, *cmd[separator:]], left_out,
                                  PROJECT_DIR / ".pio" / "analysis" / self.envname / "left-out", jobs, probe)
            if probed not in (0, 1):
                status = 2
        for pattern in stale_exclusions(excluded, unparsed):
            print(f"TIDY_STALE {self.envname} {pattern}", flush=True)
        if status not in (0, 1):
            self._bad_input = True
        print(f"TIDY_COMPLETE {self.envname} {status} {count}", flush=True)
        return {"returncode": status, "out": "", "err": ""}


if __name__ == "__main__":
    platformio.check.tools.ClangtidyCheckTool = CheckedClangTidy
    sys.exit(main())
