"""Git facts the firmware build reads: version, commit hash and working-tree state.

Every helper answers with a safe fallback instead of raising, because they run inside the
PlatformIO pre-script (scripts/git_commit_info.py) on every build. Two failures are expected
in practice and handled the same way: git exits non-zero (not a repository, unborn HEAD) and
git is not installed at all (building from an extracted tarball, or a stripped container).
When nothing can be verified the build reports version 0 and dirty=1 - unknown provenance,
never a silent claim of being clean.
"""

import subprocess

# git could not answer: either it failed, or it is not on PATH at all.
GIT_UNAVAILABLE = (subprocess.CalledProcessError, FileNotFoundError)


def get_git_hash():
    # Retrieve the short commit hash
    try:
        git_hash = int(subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip().decode('utf-8'), 16)
    except GIT_UNAVAILABLE:
        git_hash = 0
    return git_hash

def get_git_commit_count():
    # Retrieve the commit count
    try:
        git_commit_count = int(subprocess.check_output(['git', 'rev-list', '--count', 'HEAD']).strip().decode('utf-8'))
    except GIT_UNAVAILABLE:
        git_commit_count = 0
    return git_commit_count

def get_git_uncommitted_changes():
    # Check for uncommitted changes, staged ones included. A bare `git diff` compares the index
    # with the working tree, so it goes quiet again the moment a change is staged - which is
    # exactly the state right before a commit. Diffing against HEAD is what actually answers
    # "does this tree differ from the last commit".
    try:
        subprocess.check_call(["git", "diff", "--quiet", "HEAD"])
        git_uncommitted_changes = False
    except GIT_UNAVAILABLE:
        git_uncommitted_changes = True
    return git_uncommitted_changes

def get_git_untracked_files():
    # Check for untracked files
    try:
        result = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard"],
            capture_output=True, text=True, check=True
        )
        git_untracked_files = bool(result.stdout.strip())
        return git_untracked_files
    except GIT_UNAVAILABLE:
        return False

def get_git_dirty():
    # Mark the build as "dirty" if there are uncommitted or untracked changes
    git_dirty = 1 if get_git_uncommitted_changes() or get_git_untracked_files() else 0
    return git_dirty
