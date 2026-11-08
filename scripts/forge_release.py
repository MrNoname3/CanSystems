#!/usr/bin/env python3
"""Publish and fetch firmware releases through the GitHub or Gitea release API.

Both forges answer the same calls under a different base URL, which GITHUB_SERVER_URL decides:
  publish   create the release for a tag with its files and a SHA256SUMS, then delete the
            oldest v<N> releases beyond --keep (their tags stay)
  download  fetch a release's files into a directory and check them against its SHA256SUMS

Reads GITHUB_SERVER_URL, GITHUB_REPOSITORY and FORGE_TOKEN, which CI sets.
"""

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, cast

CHECKSUMS = "SHA256SUMS"
RELEASE_TAG = re.compile(r"v(\d+)")


class Forge:
    """The release endpoints of one repository."""

    def __init__(self, server: str, repository: str, token: str) -> None:
        api = "https://api.github.com" if server.rstrip("/") == "https://github.com" else server.rstrip("/") + "/api/v1"
        self.base = f"{api}/repos/{repository}"
        self.token = token

    def request(self, method: str, url: str, body: bytes | None = None,
                content_type: str = "application/json") -> Any:
        request = urllib.request.Request(url, data=body, method=method)
        request.add_header("Accept", "application/json")
        if body is not None:
            request.add_header("Content-Type", content_type)
        if self.token:
            # Not carried to a redirect: asset downloads end on storage that refuses foreign credentials.
            request.add_unredirected_header("Authorization", "token " + self.token)
        with urllib.request.urlopen(request, timeout=120) as response:
            payload = response.read()
        return json.loads(payload) if payload and response.headers.get_content_type() == "application/json" else payload

    def release(self, tag: str) -> dict[str, Any] | None:
        try:
            return cast(dict[str, Any], self.request("GET", f"{self.base}/releases/tags/{urllib.parse.quote(tag)}"))
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise

    def releases(self) -> list[dict[str, Any]]:
        return cast(list[dict[str, Any]], self.request("GET", f"{self.base}/releases?per_page=100&limit=50"))

    def delete(self, release: dict[str, Any]) -> None:
        self.request("DELETE", f"{self.base}/releases/{release['id']}")

    def create(self, tag: str) -> dict[str, Any]:
        body = json.dumps({"tag_name": tag, "name": f"CanSystems {tag}",
                           "body": "Firmware for the ESP boards; flash it from the web installer or with esptool."})
        return cast(dict[str, Any], self.request("POST", f"{self.base}/releases", body.encode()))

    def upload(self, release: dict[str, Any], name: str, data: bytes) -> None:
        url = str(release["upload_url"]).split("{")[0] + "?name=" + urllib.parse.quote(name)
        self.request("POST", url, data, "application/octet-stream")


def checksums(files: dict[str, bytes]) -> bytes:
    return "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())).encode()


def superseded(releases: list[dict[str, Any]], keep: int) -> list[dict[str, Any]]:
    """The v<N> releases beyond the newest `keep`; anything else is not this tool's to delete."""
    numbered = [(int(match[1]), release) for release in releases
                if (match := RELEASE_TAG.fullmatch(str(release.get("tag_name", ""))))]
    numbered.sort(key=lambda entry: entry[0], reverse=True)
    return [release for _, release in numbered[keep:]]


def publish(forge: Forge, tag: str, paths: list[Path], keep: int) -> None:
    files = {path.name: path.read_bytes() for path in paths}
    files[CHECKSUMS] = checksums(files)
    existing = forge.release(tag)
    if existing is not None:                                # a re-run replaces its own release
        forge.delete(existing)
    release = forge.create(tag)
    for name, data in files.items():
        forge.upload(release, name, data)
    print(f"release: published {tag} with {', '.join(files)}")
    for old in superseded([entry for entry in forge.releases() if entry.get("tag_name") != tag], keep - 1):
        forge.delete(old)
        print(f"release: deleted the superseded release {old['tag_name']}")


def download(forge: Forge, tag: str, target: Path) -> None:
    release = forge.release(tag)
    if release is None:
        raise SystemExit(f"release: no release for {tag}")
    files = {str(asset["name"]): cast(bytes, forge.request("GET", str(asset["browser_download_url"])))
             for asset in cast(list[dict[str, Any]], release["assets"])}
    listed = files.pop(CHECKSUMS, b"").decode()
    if not files or listed != checksums(files).decode():
        raise SystemExit(f"release: the files of {tag} do not match its {CHECKSUMS}")
    target.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        (target / name).write_bytes(data)
    print(f"release: downloaded {', '.join(files)} from {tag}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    publishing = commands.add_parser("publish", help="create the release and prune older ones")
    publishing.add_argument("--tag", required=True)
    publishing.add_argument("--keep", type=int, default=1, help="how many v<N> releases to keep (default 1)")
    publishing.add_argument("files", nargs="+", type=Path)
    fetching = commands.add_parser("download", help="fetch and verify a release's files")
    fetching.add_argument("--tag", required=True)
    fetching.add_argument("--into", required=True, type=Path)
    args = parser.parse_args(argv)
    if not RELEASE_TAG.fullmatch(args.tag):
        raise SystemExit("release: tags are v<commit count>")
    forge = Forge(os.environ["GITHUB_SERVER_URL"], os.environ["GITHUB_REPOSITORY"], os.environ.get("FORGE_TOKEN", ""))
    if args.command == "publish":
        if args.keep < 1:
            raise SystemExit("release: --keep must be at least 1")
        publish(forge, args.tag, args.files, args.keep)
    else:
        download(forge, args.tag, args.into)
    return 0


if __name__ == "__main__":
    sys.exit(main())
