#!/usr/bin/env python3
"""Assemble the browser firmware installer: ESP Web Tools, its manifests and one release's firmware.

  envs    print the PlatformIO environments a release builds
  assets  copy the ESP firmware PlatformIO built into the release's file names
  build   lay out the site from those files; ESP Web Tools comes from the tarball package-lock.json
          pins, checked against its integrity hash

The manifests in web/manifests name the files, so they decide which environments are released:
an ESP32 is flashed from the merged factory image at offset 0, an ESP8266 from firmware.bin.
"""

import argparse
import base64
import hashlib
import html
import io
import json
import re
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path
from typing import Any, cast

WEB_DIR = Path(__file__).resolve().parent
PROJECT_DIR = WEB_DIR.parent
ASSET = re.compile(r"\.\./firmware/cansystems-(\w+)\.bin")
RUNTIME = "package/dist/web/"


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
    for name in release_files():
        shutil.copyfile(firmware / name, output / "firmware" / name)
    shutil.copytree(WEB_DIR / "manifests", output / "manifests")
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
    assets = commands.add_parser("assets", help="copy the built firmware under its release file names")
    assets.add_argument("--build-dir", type=Path, default=PROJECT_DIR / ".pio" / "build")
    assets.add_argument("--into", type=Path, required=True)
    site = commands.add_parser("build", help="assemble the site around a release's files")
    site.add_argument("--firmware", type=Path, required=True, help="directory holding the release files")
    site.add_argument("--version", required=True, help="the release tag the page shows")
    site.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == "envs":
        print("\n".join(environment(name) for name in release_files()))
    elif args.command == "assets":
        collect_assets(args.build_dir, args.into)
    else:
        build_site(args.firmware, args.version, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
