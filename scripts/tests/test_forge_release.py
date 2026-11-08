"""Unit tests for scripts/forge_release.py: one release per tag, older ones pruned, files verified."""

import hashlib
import sys
from pathlib import Path
from typing import Any

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import forge_release`
import forge_release


class FakeForge(forge_release.Forge):
    """Answers the release calls from memory, recording every request."""

    def __init__(self, releases: list[dict[str, Any]], assets: dict[str, bytes] | None = None) -> None:
        super().__init__("https://forge.example", "owner/repo", "token")
        self.stored = releases
        self.assets = assets or {}
        self.calls: list[tuple[str, str]] = []

    def request(self, method: str, url: str, body: bytes | None = None,
                content_type: str = "application/json") -> Any:
        self.calls.append((method, url.removeprefix(self.base)))
        if method == "POST" and url.endswith("/releases"):
            release = {"id": 99, "tag_name": forge_release.json.loads(body or b"{}")["tag_name"],
                       "upload_url": self.base + "/releases/99/assets{?name,label}"}
            self.stored.append(release)
            return release
        if method == "GET" and "/releases/tags/" in url:
            tag = url.rsplit("/", 1)[1]
            found = [release for release in self.stored if release["tag_name"] == tag]
            if not found:
                raise forge_release.urllib.error.HTTPError(url, 404, "Not Found", None, None)  # type: ignore[arg-type]
            return found[0]
        if method == "GET" and url.endswith("/releases?per_page=100&limit=50"):
            return list(self.stored)
        if method == "DELETE":
            self.stored = [release for release in self.stored if not url.endswith(f"/releases/{release['id']}")]
            return b""
        if method == "GET":
            return self.assets[url]
        return b""


def test_the_api_base_follows_the_forge() -> None:
    assert forge_release.Forge("https://github.com", "o/r", "").base == "https://api.github.com/repos/o/r"
    assert forge_release.Forge("https://forge.example/", "o/r", "").base == "https://forge.example/api/v1/repos/o/r"


def test_only_numbered_releases_beyond_the_newest_are_superseded() -> None:
    releases = [{"tag_name": tag} for tag in ("v9", "v10", "nightly", "v8", "v10-rc")]
    assert [release["tag_name"] for release in forge_release.superseded(releases, 1)] == ["v9", "v8"]
    assert [release["tag_name"] for release in forge_release.superseded(releases, 2)] == ["v8"]


def test_publish_uploads_files_and_checksums_then_prunes_older_releases(tmp_path: Path) -> None:
    firmware = tmp_path / "cansystems-a.bin"
    firmware.write_bytes(b"image")
    forge = FakeForge([{"id": 1, "tag_name": "v5"}, {"id": 2, "tag_name": "v7"}, {"id": 3, "tag_name": "nightly"}])
    forge_release.publish(forge, "v8", [firmware], keep=2)
    uploads = [url for method, url in forge.calls if method == "POST" and "/assets" in url]
    assert uploads == ["/releases/99/assets?name=cansystems-a.bin", "/releases/99/assets?name=SHA256SUMS"]
    assert [release["tag_name"] for release in forge.stored] == ["v7", "nightly", "v8"]


def test_publish_keeps_only_the_new_release_by_default(tmp_path: Path) -> None:
    firmware = tmp_path / "a.bin"
    firmware.write_bytes(b"image")
    forge = FakeForge([{"id": 1, "tag_name": "v5"}, {"id": 2, "tag_name": "v9"}])
    forge_release.publish(forge, "v8", [firmware], keep=1)
    assert [release["tag_name"] for release in forge.stored] == ["v8"]


def test_a_rerun_replaces_the_release_of_its_own_tag(tmp_path: Path) -> None:
    firmware = tmp_path / "a.bin"
    firmware.write_bytes(b"image")
    forge = FakeForge([{"id": 4, "tag_name": "v8"}])
    forge_release.publish(forge, "v8", [firmware], keep=1)
    assert ("DELETE", "/releases/4") in forge.calls
    assert [release["id"] for release in forge.stored] == [99]


def _published(files: dict[str, bytes], listed: bytes) -> FakeForge:
    assets = {f"https://files.example/{name}": data for name, data in {**files, "SHA256SUMS": listed}.items()}
    release = {"id": 1, "tag_name": "v8",
               "assets": [{"name": url.rsplit("/", 1)[1], "browser_download_url": url} for url in assets]}
    return FakeForge([release], assets)


def test_download_writes_files_matching_their_checksums(tmp_path: Path) -> None:
    files = {"a.bin": b"image"}
    forge_release.download(_published(files, forge_release.checksums(files)), "v8", tmp_path)
    assert (tmp_path / "a.bin").read_bytes() == b"image"
    assert not (tmp_path / "SHA256SUMS").exists()


@pytest.mark.parametrize("listed", [b"", f"{hashlib.sha256(b'other').hexdigest()}  a.bin\n".encode()])
def test_download_refuses_files_their_checksums_do_not_list(tmp_path: Path, listed: bytes) -> None:
    with pytest.raises(SystemExit, match="SHA256SUMS"):
        forge_release.download(_published({"a.bin": b"image"}, listed), "v8", tmp_path)
    assert not (tmp_path / "a.bin").exists()


def test_tags_must_be_numbered_releases() -> None:
    with pytest.raises(SystemExit, match="v<commit count>"):
        forge_release.main(["download", "--tag", "latest", "--into", "x"])
