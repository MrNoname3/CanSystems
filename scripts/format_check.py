#!/usr/bin/env python3
"""Format guard for the release gate. Two checks, both must pass:

  1. clang-format: every tracked C/C++ file is clang-format-clean
     (`clang-format --dry-run --Werror` against the project .clang-format).
  2. final newline: every tracked text file ends with a newline, matching
     `insert_final_newline = true` in .editorconfig (clang-format itself is
     indifferent to it, so it is enforced here to keep the gate and the
     .editorconfig in harmony).

Exits 1 (listing offenders) on any violation, 0 if the whole tree is clean.
Usable standalone, as a git pre-commit hook, or as a step in release_check.py.

clang-format lookup order: $CLANG_FORMAT, then `clang-format` on PATH (CI installs
clang-format==22.1.3 via pip), then the VS Code cpptools-bundled binary (the one the
editor formats with locally). Keep the pip pin in .github/workflows/ci.yml equal to
the cpptools version so the gate never disagrees with format-on-save.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
CPP_EXTENSIONS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".tpp", ".ino", ".inl", ".ipp")

# Bases under which the cpptools extension bundles its clang-format (local fallback).
CPPTOOLS_BASES = [
    Path.home() / ".var/app/com.visualstudio.code/data/vscode/extensions",  # VS Code Flatpak
    Path.home() / ".vscode/extensions",
    Path.home() / ".vscode-server/extensions",
]


def find_clang_format() -> str:
    """Locate clang-format: explicit override, then PATH, then the cpptools bundle."""
    override = os.environ.get("CLANG_FORMAT")
    if override and Path(override).exists():
        return override
    on_path = shutil.which("clang-format")
    if on_path is not None:
        return on_path
    for base in CPPTOOLS_BASES:
        hits = sorted(base.glob("ms-vscode.cpptools-*/LLVM/bin/clang-format")) if base.is_dir() else []
        if hits:
            return str(hits[-1])
    sys.exit("clang-format not found (set $CLANG_FORMAT, put it on PATH, or install the cpptools extension)")


def git_ls_files(*patterns) -> list:
    """Tracked files matching the given pathspecs, relative to the project root."""
    result = subprocess.run(["git", "ls-files", *patterns], cwd=PROJECT_DIR,
                            capture_output=True, text=True, check=True)
    return sorted(result.stdout.split())


def is_text_file(path: Path) -> bool:
    """A regular, non-symlink file with no NUL byte in its first 8 KiB (git's heuristic)."""
    if path.is_symlink() or not path.is_file():
        return False
    with open(path, "rb") as handle:
        return b"\0" not in handle.read(8192)


def check_clang_format(clang_format: str) -> list:
    """Return the list of tracked C/C++ files that are not clang-format-clean."""
    files = git_ls_files(*(f"*{ext}" for ext in CPP_EXTENSIONS))
    if not files:
        return []
    # Fast path: one batch run; clang-format exits non-zero if any file needs changes.
    if subprocess.run([clang_format, "--dry-run", "--Werror", *files], cwd=PROJECT_DIR).returncode == 0:
        return []
    # Slow path (only on failure): pinpoint exactly which files drift.
    return [f for f in files
            if subprocess.run([clang_format, "--dry-run", "--Werror", f],
                              cwd=PROJECT_DIR, capture_output=True).returncode != 0]


def check_final_newlines() -> list:
    """Return the list of tracked text files that do not end with a newline."""
    missing = []
    for f in git_ls_files():
        path = PROJECT_DIR / f
        if not is_text_file(path):
            continue
        with open(path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            if handle.tell() == 0:
                continue                                  # empty file: no newline required
            handle.seek(-1, os.SEEK_END)
            if handle.read(1) != b"\n":
                missing.append(f)
    return missing


def main() -> int:
    clang_format = find_clang_format()
    version = subprocess.run([clang_format, "--version"], capture_output=True, text=True).stdout.strip()
    print(f"format: {version}")

    drifted = check_clang_format(clang_format)
    no_newline = check_final_newlines()

    if drifted:
        print(f"\nformat: {len(drifted)} file(s) need clang-format:")
        for path in drifted:
            print(f"  {path}")
    if no_newline:
        print(f"\nformat: {len(no_newline)} file(s) missing a final newline:")
        for path in no_newline:
            print(f"  {path}")

    if drifted or no_newline:
        print("\nFix with: clang-format -i <files>  /  append a trailing newline")
        return 1

    print("format: all clean (clang-format + final newline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
