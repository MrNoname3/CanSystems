"""PlatformIO adapter, run with its own interpreter, retaining its compiler configuration.

Only the clang-tidy driver is replaced, for this process alone. PlatformIO owns package setup,
include ordering, predefined macros and defect formatting; the adapter owns tool completion.
"""

import sys
from pathlib import Path

import platformio.check.tools
from analysis_check import CLANG_TIDY_VERSION, clang_tidy_version
from platformio.__main__ import main
from platformio.check.tools.clangtidy import ClangtidyCheckTool


class CheckedClangTidy(ClangtidyCheckTool):
    @staticmethod
    def is_check_successful(result):
        return result["returncode"] in (0, 1)

    def execute_check_cmd(self, cmd):
        if clang_tidy_version(cmd[0]) != CLANG_TIDY_VERSION:
            raise RuntimeError(f"Expected clang-tidy {CLANG_TIDY_VERSION}: {cmd[0]}")
        inputs = [arg for arg in cmd[1:cmd.index("--")] if not arg.startswith("-") and Path(arg).is_file()]
        if not inputs:
            raise RuntimeError("clang-tidy has no input files")
        result = super().execute_check_cmd(cmd)
        print(f"TIDY_COMPLETE {self.envname} {result['returncode']} {len(inputs)}", flush=True)
        return result


if __name__ == "__main__":
    platformio.check.tools.ClangtidyCheckTool = CheckedClangTidy
    sys.exit(main())
