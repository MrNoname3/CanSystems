import re

import click

from platformio.test.result import TestCase, TestStatus
from platformio.test.runners.base import TestRunnerBase


class CustomTestRunner(TestRunnerBase):
    _SUMMARY_RE = re.compile(r"\d+/\d+ tests passed")

    def on_testing_line_output(self, line):
        click.echo(line, nl=False)
        stripped = line.strip()
        if not stripped:
            return

        for marker, status in (("✓", TestStatus.PASSED), ("✗", TestStatus.FAILED)):
            if stripped.endswith(marker) and stripped.startswith("- "):
                name = stripped[2 : -len(marker)].strip()
                self.test_suite.add_case(TestCase(name=name, status=status))
                return

        if self._SUMMARY_RE.search(stripped):
            self.test_suite.on_finish()
