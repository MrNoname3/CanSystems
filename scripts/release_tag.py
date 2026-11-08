#!/usr/bin/env python3
"""Tag the pushed master head as a release: v<commit count>, the version its firmware reports.

Refuses anything but a clean master that matches origin/master, so a release is always a commit
CI has already seen. Pushing the tag starts the release jobs in .github/workflows/ci.yml.

Usage:
  python scripts/release_tag.py              # create and push the tag
  python scripts/release_tag.py --dry-run    # only say which tag it would be
"""

import argparse
import subprocess
import sys

from git_utils import get_git_commit_count, get_git_dirty

BRANCH = "master"
REMOTE = "origin"


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout.strip()


def release_tag() -> str:
    """The tag for HEAD, after checking HEAD may be released at all."""
    if git("rev-parse", "--abbrev-ref", "HEAD") != BRANCH:
        raise SystemExit(f"release: only {BRANCH} is released; check it out first")
    if get_git_dirty():
        raise SystemExit("release: the working tree has changes; a release is built from the commit alone")
    git("fetch", "--quiet", "--tags", REMOTE, BRANCH)
    if git("rev-parse", "HEAD") != git("rev-parse", f"{REMOTE}/{BRANCH}"):
        raise SystemExit(f"release: {BRANCH} differs from {REMOTE}/{BRANCH}; push or pull first")
    tag = f"v{get_git_commit_count()}"
    if git("tag", "--list", tag):
        raise SystemExit(f"release: {tag} already exists")
    return tag


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="check and print the tag without creating it")
    args = parser.parse_args(argv)
    tag = release_tag()
    if args.dry_run:
        print(f"release: would tag {git('rev-parse', '--short', 'HEAD')} as {tag}")
        return 0
    git("tag", "--annotate", tag, "--message", f"CanSystems {tag}")
    git("push", REMOTE, tag)
    print(f"release: pushed {tag}; CI builds and publishes it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
