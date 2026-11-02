"""Unit tests for scripts/secret_check.py: what counts as a leak, and where the guard looks for one.

The guard itself is excluded from its own scan, and so is this file: the samples below are the kind
of text it fails on.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import secret_check`
import secret_check


def _kinds(line: str, hosts: tuple[str, ...] = (), local: tuple[str, ...] = ()) -> list[str]:
    return secret_check.line_kinds(line, hosts, [re.compile(p) for p in local])


def test_an_address_of_a_real_network_is_a_leak() -> None:
    assert _kinds("  host: 192.168.1.10") == ["IPv4 address"]
    assert _kinds("--target 10.0.0.2:8883") == ["IPv4 address"]
    assert _kinds("the broker answers at 172.16.4.20.") == ["IPv4 address"]


def test_documentation_loopback_and_unspecified_addresses_are_not() -> None:
    assert _kinds("host: 192.0.2.10, 198.51.100.2 and 203.0.113.7") == []
    assert _kinds('bind(("0.0.0.0", port)); default ("127.0.0.1", 1883)') == []


def test_a_line_with_an_allowed_and_a_real_address_is_a_leak() -> None:
    assert _kinds("192.0.2.10 forwards to 192.168.1.10") == ["IPv4 address"]


def test_what_only_looks_like_an_address_is_not() -> None:
    assert _kinds("actionlint-py==1.7.12.25    # pinned") == []
    assert _kinds("version 1.2.3.4.5 of something") == []
    assert _kinds("999.1.1.1") == []


def test_a_mac_address_is_a_leak_unless_it_is_reserved_for_documentation() -> None:
    assert _kinds("mac: 3C:71:BF:12:34:56") == ["MAC address"]
    assert _kinds("mac: 00:00:5E:00:53:01") == []


def test_a_uuid_and_a_private_key_are_leaks() -> None:
    assert _kinds("id 123e4567-E89B-12d3-a456-426614174000") == ["UUID"]
    assert _kinds("-----BEGIN OPENSSH PRIVATE KEY-----") == ["private key"]


def test_a_private_host_is_found_in_any_case() -> None:
    assert _kinds("see https://Forge.Example.net/owner/repo", hosts=("forge.example.net",)) == ["private forge host"]


def test_a_local_pattern_is_a_leak() -> None:
    assert _kinds("broker: mqtt.lan", local=(r"\bmqtt\.lan\b",)) == ["local pattern"]
    assert _kinds("broker: mqtt.lane", local=(r"\bmqtt\.lan\b",)) == []


def test_a_hit_names_its_file_and_line() -> None:
    hits = secret_check.scan_text("ota/README.md", "a\nhost: 192.168.1.10\n", [], [])
    assert hits == [secret_check.Hit("ota/README.md", 2, "IPv4 address")]


def test_the_private_hosts_come_from_every_remote_but_the_public_forges() -> None:
    urls = [
        "https://user:token@gitea.home.example.net/owner/repo.git",
        "git@github.com:owner/repo.git",
        "ssh://git@forge.example.org:2222/owner/repo.git",
        "/srv/git/repo.git",
        "../sibling",
    ]
    assert secret_check.remote_hosts(urls) == ["example.org", "forge.example.org", "gitea.home.example.net",
                                               "home.example.net"]


def test_a_host_with_two_labels_or_an_address_has_no_domain_to_add() -> None:
    assert secret_check.remote_hosts(["https://myforge.net/o/r", "https://192.168.1.5/o/r"]) == [
        "192.168.1.5", "myforge.net"]


def test_local_patterns_skip_comments_and_refuse_a_broken_expression(tmp_path: Path) -> None:
    patterns = tmp_path / ".secret-patterns.local"
    patterns.write_text("# the broker\n\n  mqtt\\.lan  \n", encoding="utf-8")
    assert [pattern.pattern for pattern in secret_check.local_patterns(patterns)] == [r"mqtt\.lan"]
    patterns.write_text("(\n", encoding="utf-8")
    with pytest.raises(SystemExit, match="line 1|:1:"):
        secret_check.local_patterns(patterns)
    assert secret_check.local_patterns(tmp_path / "missing") == []


@pytest.mark.skipif(shutil.which("git") is None, reason="git is not installed")
def test_only_tracked_text_files_outside_the_exclusions_are_read(tmp_path: Path) -> None:
    (tmp_path / "scripts" / "tests").mkdir(parents=True)
    (tmp_path / "notes.md").write_text("text\n", encoding="utf-8")
    (tmp_path / "image.bin").write_bytes(b"\x89PNG\0\0 192.168.1.10")
    (tmp_path / "untracked.md").write_text("192.168.1.10\n", encoding="utf-8")
    (tmp_path / "scripts" / "secret_check.py").write_text("192.168.1.10\n", encoding="utf-8")
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    subprocess.run(["git", "add", "notes.md", "image.bin", "scripts/secret_check.py"], cwd=tmp_path, check=True)
    assert list(secret_check.tracked_texts(tmp_path)) == [("notes.md", "text\n")]
