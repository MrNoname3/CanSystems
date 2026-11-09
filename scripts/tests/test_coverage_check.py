"""Coverage regressions and empty measurements must fail the gate."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coverage_check as coverage


def test_coverage_regressions_fail(tmp_path: Path) -> None:
    summary, baseline = tmp_path / "summary.json", tmp_path / "baseline.json"
    baseline.write_text('{"minimum_line_percent": 90.0}')
    summary.write_text('{"line_total": 100, "line_covered": 90}')
    assert coverage.check_floor(summary, baseline)
    summary.write_text('{"line_total": 100, "line_covered": 89}')
    assert not coverage.check_floor(summary, baseline)
    summary.write_text('{"line_total": 0, "line_covered": 0}')
    with pytest.raises(ValueError, match="no valid line"):
        coverage.check_floor(summary, baseline)


def test_a_recorded_floor_leaves_a_point_of_headroom() -> None:
    assert coverage.recorded_floor(88.2) == 87.0
    assert coverage.recorded_floor(88.0) == 87.0
