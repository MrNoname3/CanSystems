#!/usr/bin/env python3
"""Secret guard for the release gate: no tracked file names a key, an address or a private host.

The repository is mirrored to a public forge, so whatever a tracked file holds is published. The
guard looks for a private key block, an IPv4 address, a MAC address and a UUID, and for the private
forge itself: the host of every git remote that is not a public forge, and the domain under it, read
from the remotes so that nothing tracked has to name them. Allowed are the addresses reserved for
documentation (RFC 5737, RFC 7042), loopback and the unspecified address, and a four-part version
pinned with `==`, which only looks like an address.

Whatever else only this machine may know - a broker's name, another internal service - goes in the
untracked .secret-patterns.local, one Python regular expression per line, `#` starting a comment.

Exits 1 listing each hit's file, line and kind, never the text itself, which would publish it in the
CI log; 0 when nothing matched.
"""

import ipaddress
import re
import subprocess
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
LOCAL_PATTERNS = PROJECT_DIR / ".secret-patterns.local"
EXCLUDED = frozenset({"scripts/secret_check.py",              # the patterns themselves,
                      "scripts/tests/test_secret_check.py"})  # and the samples they are tested on
PUBLIC_FORGES = frozenset({"github.com", "gitlab.com", "bitbucket.org", "codeberg.org", "gitea.com",
                           "git.sr.ht", "gitee.com"})

PRIVATE_KEY = re.compile(r"BEGIN [A-Z ]*PRIVATE KEY")
# Not a piece of a longer dotted number, but an address that ends a sentence still counts.
IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?!\w|\.\d)")
MAC = re.compile(r"(?<![\w:])(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}(?![\w:])")
UUID = re.compile(r"\b[0-9A-Fa-f]{8}-(?:[0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}\b")
ALLOWED_NETWORKS = tuple(ipaddress.ip_network(net) for net in (
    "192.0.2.0/24", "198.51.100.0/24", "203.0.113.0/24",      # documentation (RFC 5737)
    "127.0.0.0/8", "0.0.0.0/32"))
DOCUMENTATION_MAC = "00:00:5e:00:53:"                         # RFC 7042
HOSTNAME = re.compile(r"[a-z0-9-]+(?:\.[a-z0-9-]+)+")         # at least two labels


@dataclass(frozen=True)
class Hit:
    path: str
    line: int
    kind: str


def is_leaked_ipv4(line: str, match: re.Match[str]) -> bool:
    """Whether an IPv4-shaped match is an address that is not allowed."""
    if line[:match.start()].endswith("=="):
        return False                                          # a pinned version
    try:
        address = ipaddress.IPv4Address(match.group())
    except ValueError:
        return False                                          # an octet over 255: no address
    return not any(address in network for network in ALLOWED_NETWORKS)


def line_kinds(line: str, hosts: Iterable[str], local: Iterable[re.Pattern[str]]) -> list[str]:
    """What one line leaks, by kind."""
    lowered = line.lower()
    found = [
        ("private key", bool(PRIVATE_KEY.search(line))),
        ("IPv4 address", any(is_leaked_ipv4(line, match) for match in IPV4.finditer(line))),
        ("MAC address", any(not match.group().lower().startswith(DOCUMENTATION_MAC)
                            for match in MAC.finditer(line))),
        ("UUID", bool(UUID.search(line))),
        ("private forge host", any(host in lowered for host in hosts)),
        ("local pattern", any(pattern.search(line) for pattern in local)),
    ]
    return [kind for kind, present in found if present]


def scan_text(path: str, text: str, hosts: Iterable[str], local: Iterable[re.Pattern[str]]) -> list[Hit]:
    hosts, local = list(hosts), list(local)
    return [Hit(path, number, kind)
            for number, line in enumerate(text.splitlines(), start=1)
            for kind in line_kinds(line, hosts, local)]


def remote_hosts(urls: Iterable[str]) -> list[str]:
    """The host of every remote URL that is not a public forge, and the domain under it when that
    still has two labels: a forge's sibling services live there too."""
    names: set[str] = set()
    for url in urls:
        rest = re.sub(r"^[a-z+]+://", "", url.strip())
        rest = re.sub(r"^[^@/]+@", "", rest)                  # user and credentials
        host = re.split(r"[:/]", rest, maxsplit=1)[0].lower()
        if not HOSTNAME.fullmatch(host) or host in PUBLIC_FORGES:
            continue                                          # a local path, or published anyway
        names.add(host)
        try:
            ipaddress.ip_address(host)
        except ValueError:
            domain = host.partition(".")[2]
            if "." in domain:
                names.add(domain)
    return sorted(names)


def git_remote_urls(project_dir: Path) -> list[str]:
    result = subprocess.run(["git", "config", "--get-regexp", r"^remote\..*\.(push)?url$"],
                            cwd=project_dir, capture_output=True, text=True, check=False)
    return [line.split(maxsplit=1)[1] for line in result.stdout.splitlines() if " " in line]


def local_patterns(path: Path) -> list[re.Pattern[str]]:
    if not path.exists():
        return []
    patterns: list[re.Pattern[str]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        entry = line.strip()
        if not entry or entry.startswith("#"):
            continue
        try:
            patterns.append(re.compile(entry))
        except re.error as error:
            sys.exit(f"{path.name}:{number}: not a regular expression: {error}")
    return patterns


def tracked_texts(project_dir: Path) -> Iterator[tuple[str, str]]:
    """Every tracked text file but the excluded ones, as (path, content). A file with a NUL byte
    near its start is binary, as git itself decides."""
    listing = subprocess.run(["git", "ls-files", "-z"], cwd=project_dir,
                             capture_output=True, text=True, check=True).stdout
    for name in sorted(filter(None, listing.split("\0"))):
        path = project_dir / name
        if name in EXCLUDED or path.is_symlink() or not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" not in data[:8000]:
            yield name, data.decode("utf-8", errors="replace")


def main() -> int:
    hosts = remote_hosts(git_remote_urls(PROJECT_DIR))
    local = local_patterns(LOCAL_PATTERNS)
    hits = [hit for path, text in tracked_texts(PROJECT_DIR) for hit in scan_text(path, text, hosts, local)]
    for hit in hits:
        print(f"{hit.path}:{hit.line}: {hit.kind}")
    sources = f"{len(hosts)} private host name(s) from the remotes, {len(local)} local pattern(s)"
    if hits:
        print(f"secrets: {len(hits)} hit(s) in tracked files ({sources})")
        return 1
    print(f"secrets: clean ({sources})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
