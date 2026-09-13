#!/usr/bin/env python3
"""Format guard for the release gate. Three checks, all of which must pass:

  1. clang-format: every tracked C/C++ file is clang-format-clean
     (`clang-format --dry-run --Werror` against the project .clang-format).
  2. final newline: every tracked text file ends with a newline, matching
     `insert_final_newline = true` in .editorconfig (clang-format itself is
     indifferent to it, so it is enforced here to keep the gate and the
     .editorconfig in harmony).
  3. recorded mode: a tracked file that starts with a shebang is recorded executable,
     and one recorded executable starts with a shebang. The mode is read from git
     rather than from disk, because `core.fileMode` is off in this repository: the bit
     on disk is not the bit that gets committed, and the committed one is what every
     other checkout - the CI runner included - ends up with. No list to keep: whatever
     is tracked is checked.

Exits 1 (listing offenders) on any violation, 0 if the whole tree is clean.
Usable standalone, as a git pre-commit hook, or as a step in release_check.py.

clang-format lookup order: $CLANG_FORMAT, then `clang-format` on PATH (CI installs the pin
from requirements-dev.txt), then the VS Code cpptools-bundled binary (the one the editor
formats with locally). Keep that pin equal to the cpptools version so the gate never
disagrees with format-on-save.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
EXECUTABLE_MODE = "100755"
NOT_A_FILE_MODES = frozenset({"120000", "160000"})   # symlinks and submodule pointers
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


def git_ls_files(*patterns: str) -> list[str]:
    """Tracked files matching the given pathspecs, relative to the project root.

    Read NUL-separated, because a path is allowed to contain whitespace and every name here goes
    on to be opened or handed to clang-format as one argument."""
    result = subprocess.run(["git", "ls-files", "-z", *patterns], cwd=PROJECT_DIR,
                            capture_output=True, text=True, check=True)
    return sorted(name for name in result.stdout.split("\0") if name)


def git_ls_files_with_modes() -> list[tuple[str, str]]:
    """Every tracked file as (mode git has recorded, path), NUL-separated for the same reason."""
    result = subprocess.run(["git", "ls-files", "-sz"], cwd=PROJECT_DIR,
                            capture_output=True, text=True, check=True)
    entries: list[tuple[str, str]] = []
    for entry in result.stdout.split("\0"):
        if not entry:
            continue
        meta, _, name = entry.partition("\t")
        entries.append((meta.split()[0], name))
    return sorted(entries, key=lambda entry: entry[1])


def starts_with_shebang(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with open(path, "rb") as handle:
        return handle.read(2) == b"#!"


def check_recorded_modes() -> tuple[list[str], list[str]]:
    """Tracked files whose recorded mode and shebang disagree: (unrunnable, executable for nothing)."""
    unrunnable: list[str] = []
    pointless: list[str] = []
    for mode, name in git_ls_files_with_modes():
        path = PROJECT_DIR / name
        if mode in NOT_A_FILE_MODES or not path.is_file():
            continue                                      # tracked, but nothing here to read
        if starts_with_shebang(path):
            if mode != EXECUTABLE_MODE:
                unrunnable.append(name)
        elif mode == EXECUTABLE_MODE:
            pointless.append(name)
    return unrunnable, pointless


def is_text_file(path: Path) -> bool:
    """A regular, non-symlink file with no NUL byte in its first 8 KiB (git's heuristic)."""
    if path.is_symlink() or not path.is_file():
        return False
    with open(path, "rb") as handle:
        return b"\0" not in handle.read(8192)


def check_clang_format(clang_format: str) -> list[str]:
    """Return the list of tracked C/C++ files that are not clang-format-clean."""
    files = git_ls_files(*(f"*{ext}" for ext in CPP_EXTENSIONS))
    if not files:
        return []
    # Fast path: one batch run; clang-format exits non-zero if any file needs changes.
    if subprocess.run([clang_format, "--dry-run", "--Werror", *files], cwd=PROJECT_DIR, check=False).returncode == 0:
        return []
    # Slow path (only on failure): pinpoint exactly which files drift.
    return [f for f in files
            if subprocess.run([clang_format, "--dry-run", "--Werror", f],
                              cwd=PROJECT_DIR, capture_output=True, check=False).returncode != 0]


def check_final_newlines() -> list[str]:
    """Return the list of tracked text files that do not end with a newline."""
    missing: list[str] = []
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
    version = subprocess.run([clang_format, "--version"], capture_output=True, text=True, check=False).stdout.strip()
    print(f"format: {version}")

    drifted = check_clang_format(clang_format)
    no_newline = check_final_newlines()
    unrunnable, pointless = check_recorded_modes()

    if drifted:
        print(f"\nformat: {len(drifted)} file(s) need clang-format:")
        for path in drifted:
            print(f"  {path}")
    if no_newline:
        print(f"\nformat: {len(no_newline)} file(s) missing a final newline:")
        for path in no_newline:
            print(f"  {path}")
    if unrunnable:
        print(f"\nformat: {len(unrunnable)} file(s) start with a shebang but are not recorded executable:")
        for path in unrunnable:
            print(f"  {path}")
        print("  fix with: git update-index --chmod=+x <files>")
    if pointless:
        print(f"\nformat: {len(pointless)} file(s) are recorded executable but have no shebang:")
        for path in pointless:
            print(f"  {path}")
        print("  fix with: git update-index --chmod=-x <files>")

    if drifted or no_newline or unrunnable or pointless:
        print("\nFix with: clang-format -i <files>  /  append a trailing newline  /  git update-index --chmod")
        return 1

    print("format: all clean (clang-format + final newline + recorded mode)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
