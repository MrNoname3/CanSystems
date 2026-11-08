#!/usr/bin/env python3
"""Assemble the browser firmware installer: ESP Web Tools, its manifests and one release's firmware.

  envs    print the PlatformIO environments a release builds
  assets  copy the ESP firmware PlatformIO built into the release's file names, and build each
          environment's configuration template with it
  build   lay out the site from those files; ESP Web Tools comes from the tarball package-lock.json
          pins, checked against its integrity hash

The manifests in web/manifests name the files, so they decide which environments are released:
an ESP32 is flashed from the merged factory image at offset 0, an ESP8266 from firmware.bin.

A configuration template is the environment's LittleFS image as `pio run -t buildfs` builds it,
holding a placeholder for each /config file devices.yaml gives the environment's devices. Every
placeholder fills exactly one block, so fspatch.mjs can put a board's own configuration into it in
the browser; cansystems-config.json tells the page where each image goes and what it holds.
"""

import argparse
import base64
import hashlib
import html
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from typing import IO, Any, Protocol, cast

WEB_DIR = Path(__file__).resolve().parent
PROJECT_DIR = WEB_DIR.parent
ASSET = re.compile(r"\.\./firmware/cansystems-(\w+)\.bin")
RUNTIME = "package/dist/web/"
CONFIG = "cansystems-config.json"
PAGE_FILES = ("index.html", "config.js", "fspatch.mjs")
# The /config files config.js knows how to fill; a template holding any other has nothing to put there.
FILLABLE = ("/config/mosq-ca.crt", "/config/server.json", "/config/tube.json")
ESP8266_FLASH_BASE = 0x40200000              # where the ESP8266 maps flash offset 0
ESP32_FS_BLOCK = 0x1000                      # the pioarduino builder's LittleFS block size
ESP32_FS_SUBTYPES = ("spiffs", "littlefs", "fat")


def manifests() -> dict[str, dict[str, Any]]:
    return {path.name: cast(dict[str, Any], json.loads(path.read_text()))
            for path in sorted((WEB_DIR / "manifests").glob("*.json"))}


def release_files() -> dict[str, str]:
    """Each release file name with the chip family it is built for."""
    files: dict[str, str] = {}
    for name, manifest in manifests().items():
        for build in cast(list[dict[str, Any]], manifest["builds"]):
            for part in cast(list[dict[str, Any]], build["parts"]):
                if not ASSET.fullmatch(part["path"]) or part["offset"] != 0:
                    raise ValueError(f"{name}: a part must be ../firmware/cansystems-<env>.bin at offset 0")
                files[part["path"].removeprefix("../firmware/")] = build["chipFamily"]
    return files


def environment(name: str) -> str:
    return cast(re.Match[str], ASSET.fullmatch("../firmware/" + name))[1]


def collect_assets(build_dir: Path, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    for name, chip in release_files().items():
        env = environment(name)
        image = build_dir / env / ("firmware.factory.bin" if chip == "ESP32" else "firmware.bin")
        shutil.copyfile(image, target / name)
        print(f"installer: {image.relative_to(build_dir)} -> {name}")


def template_name(env: str) -> str:
    return f"cansystems-{env}.littlefs.bin"


def published_files() -> list[str]:
    """Every file a release carries besides SHA256SUMS."""
    envs = [environment(name) for name in release_files()]
    return [*release_files(), *(template_name(env) for env in envs), CONFIG]


def placeholder(path: str, block_size: int) -> bytes:
    """A file that owns exactly one block and names the /config path it stands for."""
    marker = f"CANSYSTEMS-TEMPLATE {path}\n".encode()
    return marker + b"." * (block_size - len(marker))


def config_paths(projects: list[Any], env: str) -> list[str]:
    """The /config files devices.yaml gives the devices built from `env`, as provisioning writes them."""
    paths = sorted({entry.device_path for project in projects if project.pio_project == env
                    for device in project.devices for entry in device.files
                    if entry.device_path.startswith("/config/")})
    if not paths or not set(paths) <= set(FILLABLE):
        raise ValueError(f"installer: {env} needs config files the page can fill, has {paths}")
    return paths


def esp32_fs_area(partitions: str) -> tuple[int, int]:
    """Offset and size of the filesystem partition, the last data one as the pioarduino builder picks it."""
    rows = [[cell.strip() for cell in line.split(",")] for line in partitions.splitlines()
            if line.strip() and not line.lstrip().startswith("#")]
    areas = [(int(row[3], 0), int(row[4], 0)) for row in rows if row[1] == "data" and row[2] in ESP32_FS_SUBTYPES]
    if not areas:
        raise ValueError("installer: the partition table has no filesystem partition")
    return areas[-1]


def esp8266_fs_area(ldscript: str) -> tuple[int, int, int]:
    """Offset, size and block size of the filesystem the linker script lays out."""
    symbols = {name: int(value, 0) for name, value in re.findall(r"PROVIDE\s*\(\s*_FS_(\w+)\s*=\s*(\w+)\s*\)", ldscript)}
    return symbols["start"] - ESP8266_FLASH_BASE, symbols["end"] - symbols["start"], symbols["block"]


def fs_area(options: dict[str, str]) -> tuple[int, int, int]:
    """Flash offset, size and block size of an environment's filesystem."""
    if "board_build.partitions" in options:
        offset, size = esp32_fs_area((PROJECT_DIR / options["board_build.partitions"]).read_text())
        return offset, size, ESP32_FS_BLOCK
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    ldscripts = core / "packages" / "framework-arduinoespressif8266" / "tools" / "sdk" / "ld"
    return esp8266_fs_area((ldscripts / options["board_build.ldscript"]).read_text())


def fill(image: bytes, block_size: int, files: dict[str, bytes]) -> bytes:
    """Run fspatch.mjs over an image, as the page does."""
    request = {"image": base64.b64encode(image).decode(), "blockSize": block_size,
               "files": {path: base64.b64encode(data).decode() for path, data in files.items()}}
    script = ("const fs = await import('node:fs'); const { fillTemplate } = await import('./fspatch.mjs');"
              "const r = JSON.parse(fs.readFileSync(0, 'utf8'));"
              "const files = new Map(Object.entries(r.files).map(([p, d]) => [p, Buffer.from(d, 'base64')]));"
              "process.stdout.write(Buffer.from(fillTemplate(Buffer.from(r.image, 'base64'), r.blockSize, files))"
              ".toString('base64'));")
    result = subprocess.run(["node", "--input-type=module", "-e", script], input=json.dumps(request), cwd=WEB_DIR,
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise ValueError(f"installer: fspatch.mjs failed: {result.stderr.strip()}")
    return base64.b64decode(result.stdout)


class LittleFiles(Protocol):
    """littlefs.LittleFS's open(), which the package types as returning IO[Unknown]."""

    def open(self, fname: str, mode: str = "r") -> IO[bytes]: ...


def check_template(image: bytes, block_size: int, paths: list[str]) -> None:
    """Fill the template the way the page does and read it back with LittleFS itself."""
    from littlefs import LittleFS  # imported here: `build` runs where only the standard library is

    samples = {path: bytes([65 + index]) * (1 + index * (block_size - 1) // max(1, len(paths) - 1))
               for index, path in enumerate(paths)}
    filled = fill(image, block_size, samples)
    fs = LittleFS(block_size=block_size, block_count=len(filled) // block_size, mount=False)
    fs.context.buffer = bytearray(filled)
    fs.mount()
    for path, data in samples.items():
        with cast(LittleFiles, fs).open(path, "rb") as file:
            if file.read() != data:
                raise ValueError(f"installer: {path} does not read back from a filled template")


def build_templates(target: Path, pio: str) -> None:
    """Each environment's configuration template, checked, and the description the page reads."""
    sys.path.insert(0, str(PROJECT_DIR / "ota"))
    import otaUpdate  # imported here, as littlefs is above

    projects = otaUpdate.DeviceManager(str(PROJECT_DIR / "ota" / "otaUpdate.py")).load()
    listing = subprocess.run([pio, "project", "config", "--json-output"], cwd=PROJECT_DIR, check=True,
                             capture_output=True, text=True, env={**os.environ, "VIRTUAL_ENV": ""}).stdout
    sections = {section: dict(options) for section, options in json.loads(listing)}
    devices: dict[str, Any] = {}
    for name in release_files():
        env = environment(name)
        paths = config_paths(projects, env)
        offset, size, block_size = fs_area(sections[f"env:{env}"])
        with tempfile.TemporaryDirectory() as data:
            for path in paths:
                (Path(data) / path.lstrip("/")).parent.mkdir(parents=True, exist_ok=True)
                (Path(data) / path.lstrip("/")).write_bytes(placeholder(path, block_size))
            subprocess.run([pio, "run", "-e", env, "-t", "buildfs"], cwd=PROJECT_DIR, check=True,
                           env={**os.environ, "VIRTUAL_ENV": "", "PLATFORMIO_DATA_DIR": data})
        image = (PROJECT_DIR / ".pio" / "build" / env / "littlefs.bin").read_bytes()
        if len(image) != size:
            raise ValueError(f"installer: {env}'s image is {len(image)} bytes, its filesystem {size}")
        check_template(image, block_size, paths)
        (target / template_name(env)).write_bytes(image)
        devices[env] = {"image": template_name(env), "offset": offset, "blockSize": block_size, "files": paths}
        print(f"installer: {template_name(env)} at {offset:#x}, holding {', '.join(paths)}")
    ca_bundle = otaUpdate.extract_system_ca_roots(otaUpdate.ConfigManager.DEFAULT_CA_ROOTS).decode()
    (target / CONFIG).write_text(json.dumps({"serverJson": otaUpdate.server_json_rules(), "caBundle": ca_bundle,
                                             "devices": devices}, indent=1) + "\n")


def runtime_tarball() -> bytes:
    """The ESP Web Tools tarball package-lock.json pins, after checking its integrity hash."""
    lock = json.loads((WEB_DIR / "package-lock.json").read_text())
    entry = lock["packages"]["node_modules/esp-web-tools"]
    algorithm, expected = str(entry["integrity"]).split("-", 1)
    request = urllib.request.Request(entry["resolved"], headers={"User-Agent": "CanSystems-web-site"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = cast(bytes, response.read())
    if base64.b64encode(hashlib.new(algorithm, data).digest()).decode() != expected:
        raise SystemExit(f"installer: {entry['resolved']} does not match package-lock.json")
    return data


def build_site(firmware: Path, version: str, output: Path) -> None:
    if output.exists():
        shutil.rmtree(output)
    (output / "firmware").mkdir(parents=True)
    for name in published_files():
        shutil.copyfile(firmware / name, output / "firmware" / name)
    shutil.copytree(WEB_DIR / "manifests", output / "manifests")
    for name in PAGE_FILES:
        shutil.copyfile(WEB_DIR / name, output / name)
    page = (WEB_DIR / "index.html").read_text()
    (output / "index.html").write_text(page.replace("@VERSION@", html.escape(version)))
    with tarfile.open(fileobj=io.BytesIO(runtime_tarball()), mode="r:gz") as package:
        members = [member for member in package.getmembers() if member.isfile() and member.name.startswith(RUNTIME)]
        for member in members:
            member.name = member.name.removeprefix(RUNTIME)
        package.extractall(output / "esp-web-tools", members=members, filter="data")
    (output / ".nojekyll").touch()
    print(f"installer: {version} ready in {output}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("envs", help="print the environments a release builds, one per line")
    assets = commands.add_parser("assets", help="copy the built firmware and build the configuration templates")
    assets.add_argument("--build-dir", type=Path, default=PROJECT_DIR / ".pio" / "build")
    assets.add_argument("--into", type=Path, required=True)
    assets.add_argument("--pio", default="pio", help="the PlatformIO command (default: pio)")
    site = commands.add_parser("build", help="assemble the site around a release's files")
    site.add_argument("--firmware", type=Path, required=True, help="directory holding the release files")
    site.add_argument("--version", required=True, help="the release tag the page shows")
    site.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == "envs":
        print("\n".join(environment(name) for name in release_files()))
    elif args.command == "assets":
        collect_assets(args.build_dir, args.into)
        build_templates(args.into, args.pio)
    else:
        build_site(args.firmware, args.version, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
