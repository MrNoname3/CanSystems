"""The web installer's release files, firmware collection and site layout."""

import base64
import hashlib
import io
import shutil
import struct
import sys
import tarfile
from pathlib import Path
from typing import Any, cast

import pytest
from littlefs import LittleFS

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import installer


def _manifest(path: str, chip: str = "ESP8266", offset: int = 0, *more: tuple[str, int]) -> dict[str, Any]:
    parts = [{"path": part, "offset": at} for part, at in ((path, offset), *more)]
    return {"name": "x", "builds": [{"chipFamily": chip, "parts": parts}]}


def _esp32(env: str, app_offset: int = 0xE000) -> dict[str, Any]:
    return _manifest(f"../firmware/cansystems-{env}.boot.bin", "ESP32", 0,
                     (f"../firmware/cansystems-{env}.bin", app_offset))


def test_the_manifests_release_each_esp_environment_once() -> None:
    assert installer.release_files() == {
        "cansystems-project_esp32_can.boot.bin": ("ESP32", 0),
        "cansystems-project_esp32_can.bin": ("ESP32", 0xE000),
        "cansystems-project_esp8266_rad.bin": ("ESP8266", 0),
        "cansystems-project_esp8266_thermo.bin": ("ESP8266", 0),
    }
    assert installer.environments() == ["project_esp32_can", "project_esp8266_rad", "project_esp8266_thermo"]


@pytest.mark.parametrize("manifest", [_manifest("../firmware/other.bin"), _manifest("firmware/cansystems-x.bin"),
                                      _manifest("../firmware/cansystems-x.bin", offset=0x10000),
                                      _manifest("../firmware/cansystems-x.bin", "ESP32"),
                                      _manifest("../firmware/cansystems-x.boot.bin", "ESP8266"),
                                      _manifest("../firmware/cansystems-x.boot.bin", "ESP32", 0,
                                                ("../firmware/cansystems-y.bin", 0xE000))])
def test_a_part_outside_the_release_layout_is_refused(monkeypatch: pytest.MonkeyPatch, manifest: dict[str, Any]) -> None:
    monkeypatch.setattr(installer, "manifests", lambda: {"bad.json": manifest})
    with pytest.raises(ValueError, match="bad.json"):
        installer.release_files()


def _factory() -> bytes:
    """A factory image whose partition table puts NVS at 0x9000-0xE000, each region marked."""
    image = bytearray(b"B" * 0x9000 + b"N" * 0x5000 + b"A" * 0x3000)
    for index, (kind, subtype, offset, size) in enumerate(((0x01, 0x02, 0x9000, 0x5000), (0x01, 0x00, 0xE000, 0x2000))):
        struct.pack_into("<HBBII16sI", image, 0x8000 + 32 * index, 0x50AA, kind, subtype, offset, size, b"p", 0)
    image[0x8000 + 64:0x8000 + 96] = b"\xff" * 32
    return bytes(image)


def _build(root: Path) -> Path:
    for env, name, data in (("gateway", "firmware.factory.bin", _factory()), ("gateway", "firmware.bin", b"app"),
                            ("sensor", "firmware.bin", b"sensor/firmware.bin")):
        (root / env).mkdir(parents=True, exist_ok=True)
        (root / env / name).write_bytes(data)
    return root


def test_esp32_releases_its_factory_image_around_nvs_and_esp8266_the_application(
        tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(installer, "manifests", lambda: {"a.json": _esp32("gateway"),
                                                         "b.json": _manifest("../firmware/cansystems-sensor.bin")})
    installer.collect_assets(_build(tmp_path / "build"), tmp_path / "release")
    factory = _factory()
    assert (tmp_path / "release" / "cansystems-gateway.boot.bin").read_bytes() == factory[:0x9000]
    assert (tmp_path / "release" / "cansystems-gateway.bin").read_bytes() == factory[0xE000:]
    assert (tmp_path / "release" / "cansystems-sensor.bin").read_bytes() == b"sensor/firmware.bin"


def test_an_esp32_application_not_placed_where_nvs_ends_is_refused(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(installer, "manifests", lambda: {"a.json": _esp32("gateway", 0x10000)})
    with pytest.raises(ValueError, match="0xe000"):
        installer.collect_assets(_build(tmp_path / "build"), tmp_path / "release")


def test_a_factory_image_without_nvs_is_refused() -> None:
    with pytest.raises(ValueError, match="no NVS"):
        installer.esp32_nvs_area(b"\xff" * 0x9000)


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
    for name in installer.published_files():
        (firmware / name).write_bytes(name.encode())
    output = tmp_path / "site"
    installer.build_site(firmware, "v1<2>", output)
    assert sorted(path.name for path in (output / "esp-web-tools").iterdir()) == ["chunk.js", "install-button.js"]
    assert "Release v1&lt;2&gt;" in (output / "index.html").read_text()
    assert "@VERSION@" not in (output / "index.html").read_text()
    for name in installer.published_files():
        assert (output / "firmware" / name).read_bytes() == name.encode()
    for name in ("config.js", "fspatch.mjs"):
        assert (output / name).read_bytes() == (installer.WEB_DIR / name).read_bytes()
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


def test_a_release_carries_each_environments_template_and_the_page_description() -> None:
    assert installer.published_files() == [
        *installer.release_files(), "cansystems-project_esp32_can.littlefs.bin",
        "cansystems-project_esp8266_rad.littlefs.bin", "cansystems-project_esp8266_thermo.littlefs.bin",
        "cansystems-config.json"]


def _projects() -> list[Any]:
    sys.path.insert(0, str(installer.PROJECT_DIR / "ota"))
    import otaUpdate

    return otaUpdate.DeviceManager(str(installer.PROJECT_DIR / "ota" / "otaUpdate.py")).load()


def test_a_template_holds_the_config_files_provisioning_writes() -> None:
    projects = _projects()
    assert installer.config_paths(projects, "project_esp8266_thermo") == ["/config/mosq-ca.crt", "/config/server.json"]
    assert installer.config_paths(projects, "project_esp8266_rad") == [
        "/config/mosq-ca.crt", "/config/server.json", "/config/tube.json"]


def test_a_config_file_the_page_cannot_fill_is_refused() -> None:
    projects = _projects()
    projects[0].devices[0].files[0].device_path = "/config/other.json"
    with pytest.raises(ValueError, match="can fill"):
        installer.config_paths(projects, projects[0].pio_project)


def test_the_filesystem_areas_come_from_the_partition_table_and_the_linker_script() -> None:
    assert installer.esp32_fs_area((installer.PROJECT_DIR / "partitions_esp32_4mb.csv").read_text()) == (0x350000, 0xA0000)
    ldscript = ("PROVIDE ( _FS_start = 0x40400000 );\nPROVIDE ( _FS_end = 0x405FA000 );\n"
                "PROVIDE ( _FS_page = 0x100 );\nPROVIDE ( _FS_block = 0x2000 );\n")
    assert installer.esp8266_fs_area(ldscript) == (0x200000, 0x1FA000, 0x2000)


needs_node = pytest.mark.skipif(shutil.which("node") is None, reason="fspatch.mjs runs under node")


def _template(block_size: int, paths: list[str]) -> bytes:
    """A configuration template as the ESP32 builder lays one out, at the given block size."""
    fs = LittleFS(block_size=block_size, block_count=32, read_size=1, prog_size=1, cache_size=block_size,
                  lookahead_size=32, name_max=64, disk_version=(2 << 16) | 1)
    fs.makedirs("config", exist_ok=True)
    for path in paths:
        with cast(installer.LittleFiles, fs).open(path, "wb") as file:
            file.write(installer.placeholder(path, block_size))
    return bytes(fs.context.buffer)


@needs_node
@pytest.mark.parametrize("block_size", [0x1000, 0x2000])
def test_filled_placeholders_read_back_at_their_own_size(block_size: int) -> None:
    paths = ["/config/mosq-ca.crt", "/config/server.json", "/config/tube.json"]
    installer.check_template(_template(block_size, paths), block_size, paths)
    files = {"/config/server.json": '{"ssid":"Wi-Fi ő"}'.encode(), "/config/tube.json": b'{"tube":2}',
             "/config/mosq-ca.crt": b"P" * block_size}
    filled = installer.fill(_template(block_size, paths), block_size, files)
    fs = LittleFS(block_size=block_size, block_count=len(filled) // block_size, mount=False)
    fs.context.buffer = bytearray(filled)
    fs.mount()
    for path, data in files.items():
        assert fs.stat(path).size == len(data)
        with cast(installer.LittleFiles, fs).open(path, "rb") as file:
            assert file.read() == data


@needs_node
@pytest.mark.parametrize(("files", "message"), [
    ({"/config/server.json": b""}, "1 to 4096 bytes"),
    ({"/config/server.json": b"x" * 4097}, "1 to 4096 bytes"),
    ({"/config/tube.json": b"{}"}, "0 placeholders"),
])
def test_content_without_a_placeholder_to_hold_it_is_refused(files: dict[str, bytes], message: str) -> None:
    with pytest.raises(ValueError, match=message):
        installer.fill(_template(0x1000, ["/config/server.json"]), 0x1000, files)
