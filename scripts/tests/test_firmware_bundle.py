"""Only complete, intact firmware from the checked commit can be reused by a release."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import firmware_bundle as firmware


def test_verified_firmware_round_trips_and_corruption_fails(tmp_path: Path) -> None:
    build, bundle, restored = (tmp_path / name for name in ("build", "bundle", "restored"))
    for name in firmware.files("esp8266"):
        path = build / name
        path.parent.mkdir(parents=True)
        path.write_bytes(b"checked firmware")
    firmware.export(build, bundle, "esp8266", "commit-a")
    with pytest.raises(ValueError, match="provenance"):
        firmware.restore(bundle, restored, "esp8266", "commit-b")
    firmware.restore(bundle, restored, "esp8266", "commit-a")
    for name in firmware.files("esp8266"):
        assert (restored / name).read_bytes() == (build / name).read_bytes()
    (bundle / firmware.files("esp8266")[0]).write_bytes(b"corrupt")
    with pytest.raises(ValueError, match="checksum"):
        firmware.restore(bundle, restored, "esp8266", "commit-a")
