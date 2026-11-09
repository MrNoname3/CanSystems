"""Transfer checked firmware between jobs using the cache API supported by both forges.

A bundle names its exact commit and group and hashes every expected binary. Cache misses may
rebuild through the release gate; a present but invalid bundle always fails verification.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
from typing import cast

from gate_groups import GROUPS

RELEASE_GROUPS = ("esp32", "esp8266")


def files(group: str) -> list[str]:
    suffix = "firmware.factory.bin" if group == "esp32" else "firmware.bin"
    return [f"{env}/{suffix}" for env in GROUPS[group].envs]


def export(build: Path, bundle: Path, group: str, commit: str) -> None:
    bundle.mkdir(parents=True, exist_ok=True)
    hashes: dict[str, str] = {}
    for name in files(group):
        source, target = build / name, bundle / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    (bundle / "manifest.json").write_text(json.dumps({"commit": commit, "group": group, "files": hashes}) + "\n")


def restore(bundle: Path, build: Path, group: str, commit: str) -> None:
    manifest = cast(dict[str, object], json.loads((bundle / "manifest.json").read_text()))
    if manifest.get("commit") != commit or manifest.get("group") != group:
        raise ValueError("Firmware bundle provenance differs from this release")
    hashes = manifest.get("files")
    if not isinstance(hashes, dict) or set(cast(dict[str, object], hashes)) != set(files(group)):
        raise ValueError("Firmware bundle does not contain exactly the expected binaries")
    expected = cast(dict[str, str], hashes)
    for name in files(group):
        if hashlib.sha256((bundle / name).read_bytes()).hexdigest() != expected[name]:
            raise ValueError(f"Firmware checksum mismatch: {name}")
    for name in files(group):
        target = build / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(bundle / name, target)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("export", "restore"))
    parser.add_argument("--group", choices=RELEASE_GROUPS, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path(".pio/build"))
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if args.operation == "export":
        export(args.build, args.bundle, args.group, commit)
    else:
        restore(args.bundle, args.build, args.group, commit)


if __name__ == "__main__":
    main()
