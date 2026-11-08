"""The web installer's release files, firmware collection and site layout."""

import base64
import hashlib
import io
import sys
import tarfile
from pathlib import Path
from typing import Any

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import installer


def _manifest(path: str, chip: str = "ESP8266", offset: int = 0) -> dict[str, Any]:
    return {"name": "x", "builds": [{"chipFamily": chip, "parts": [{"path": path, "offset": offset}]}]}


def test_the_manifests_release_each_esp_environment_once() -> None:
    assert installer.release_files() == {
        "cansystems-project_esp32_can.bin": "ESP32",
        "cansystems-project_esp8266_rad.bin": "ESP8266",
        "cansystems-project_esp8266_thermo.bin": "ESP8266",
    }


@pytest.mark.parametrize("manifest", [_manifest("../firmware/other.bin"), _manifest("firmware/cansystems-x.bin"),
                                      _manifest("../firmware/cansystems-x.bin", offset=0x10000)])
def test_a_part_outside_the_release_layout_is_refused(monkeypatch: pytest.MonkeyPatch, manifest: dict[str, Any]) -> None:
    monkeypatch.setattr(installer, "manifests", lambda: {"bad.json": manifest})
    with pytest.raises(ValueError, match="bad.json"):
        installer.release_files()


def test_esp32_releases_the_factory_image_and_esp8266_the_application(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(installer, "manifests", lambda: {
        "a.json": _manifest("../firmware/cansystems-gateway.bin", "ESP32"),
        "b.json": _manifest("../firmware/cansystems-sensor.bin", "ESP8266")})
    for env, name in (("gateway", "firmware.factory.bin"), ("gateway", "firmware.bin"), ("sensor", "firmware.bin")):
        (tmp_path / "build" / env).mkdir(parents=True, exist_ok=True)
        (tmp_path / "build" / env / name).write_bytes(f"{env}/{name}".encode())
    installer.collect_assets(tmp_path / "build", tmp_path / "release")
    assert (tmp_path / "release" / "cansystems-gateway.bin").read_bytes() == b"gateway/firmware.factory.bin"
    assert (tmp_path / "release" / "cansystems-sensor.bin").read_bytes() == b"sensor/firmware.bin"


def _tarball() -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
        for name, data in (("package/dist/web/install-button.js", b"runtime"), ("package/dist/web/chunk.js", b"chunk"),
                           ("package/dist/install-button.js", b"not for browsers"), ("package/package.json", b"{}")):
            member = tarfile.TarInfo(name)
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    return buffer.getvalue()


def test_the_site_carries_the_release_runtime_and_version(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(installer, "runtime_tarball", _tarball)
    firmware = tmp_path / "release"
    firmware.mkdir()
    for name in installer.release_files():
        (firmware / name).write_bytes(name.encode())
    output = tmp_path / "site"
    installer.build_site(firmware, "v1<2>", output)
    assert sorted(path.name for path in (output / "esp-web-tools").iterdir()) == ["chunk.js", "install-button.js"]
    assert "Release v1&lt;2&gt;" in (output / "index.html").read_text()
    assert "@VERSION@" not in (output / "index.html").read_text()
    for name in installer.release_files():
        assert (output / "firmware" / name).read_bytes() == name.encode()
    assert sorted(path.name for path in (output / "manifests").iterdir()) == sorted(
        path.name for path in (installer.WEB_DIR / "manifests").iterdir())


def test_a_missing_release_file_fails_the_build(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(installer, "runtime_tarball", _tarball)
    (tmp_path / "release").mkdir()
    with pytest.raises(FileNotFoundError):
        installer.build_site(tmp_path / "release", "v1", tmp_path / "site")


class _Response(io.BytesIO):
    def __enter__(self) -> "_Response":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


@pytest.mark.parametrize("tampered", [False, True])
def test_the_runtime_must_match_its_locked_integrity(monkeypatch: pytest.MonkeyPatch, tampered: bool) -> None:
    data = _tarball()
    lock = installer.json.loads((installer.WEB_DIR / "package-lock.json").read_text())
    entry = lock["packages"]["node_modules/esp-web-tools"]
    entry["integrity"] = "sha512-" + base64.b64encode(hashlib.sha512(data).digest()).decode()
    def locked(_text: str) -> Any:
        return lock

    def serve(_request: object, timeout: float) -> _Response:
        assert timeout > 0
        return _Response(data + (b"x" if tampered else b""))

    monkeypatch.setattr(installer.json, "loads", locked)
    monkeypatch.setattr(installer.urllib.request, "urlopen", serve)
    if tampered:
        with pytest.raises(SystemExit, match="package-lock.json"):
            installer.runtime_tarball()
    else:
        assert installer.runtime_tarball() == data
