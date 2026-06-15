#!/usr/bin/env python3
"""Format guard: fail if any tracked C/C++ file is not clang-format-clean.

Runs `clang-format --dry-run --Werror` over every git-tracked C/C++ source file
against the project .clang-format. Exits 1 (listing the offending files) if any
file would be reformatted, 0 if the whole tree is clean. Usable standalone, as a
git pre-commit hook, or as a step in release_check.py (so CI enforces it too).

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
EXTENSIONS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".tpp", ".ino", ".inl", ".ipp")

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


def tracked_files() -> list:
    """Every git-tracked C/C++ source file, relative to the project root."""
    patterns = [f"*{ext}" for ext in EXTENSIONS]
    result = subprocess.run(["git", "ls-files", *patterns], cwd=PROJECT_DIR,
                            capture_output=True, text=True, check=True)
    return sorted(result.stdout.split())


def main() -> int:
    clang_format = find_clang_format()
    files = tracked_files()
    if not files:
        print("format: no tracked C/C++ files")
        return 0

    version = subprocess.run([clang_format, "--version"], capture_output=True, text=True).stdout.strip()
    print(f"format: {version}")
    print(f"format: checking {len(files)} tracked C/C++ files")

    # Fast path: one batch run; clang-format exits non-zero if any file needs changes.
    batch = subprocess.run([clang_format, "--dry-run", "--Werror", *files], cwd=PROJECT_DIR)
    if batch.returncode == 0:
        print("format: all clean")
        return 0

    # Slow path (only on failure): pinpoint exactly which files drift.
    drifted = [f for f in files
               if subprocess.run([clang_format, "--dry-run", "--Werror", f],
                                 cwd=PROJECT_DIR, capture_output=True).returncode != 0]
    print(f"\nformat: {len(drifted)} file(s) need clang-format:")
    for path in drifted:
        print(f"  {path}")
    print("\nFix with: clang-format -i <files>  (or your editor's Format Document)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
