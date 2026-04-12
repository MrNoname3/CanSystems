import subprocess

def get_git_hash():
    # Retrieve the short commit hash
    try:
        git_hash = int(subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip().decode('utf-8'), 16)
    except subprocess.CalledProcessError:
        git_hash = 0
    return git_hash

def get_git_commit_count():
    # Retrieve the commit count
    try:
        git_commit_count = int(subprocess.check_output(['git', 'rev-list', '--count', 'HEAD']).strip().decode('utf-8'))
    except subprocess.CalledProcessError:
        git_commit_count = 0
    return git_commit_count

def get_git_uncommitted_changes():
    # Check for uncommitted changes
    try:
        subprocess.check_call(["git", "diff", "--quiet"])
        git_uncommitted_changes = False
    except subprocess.CalledProcessError:
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
    except Exception:
        return False

def get_git_dirty():
    # Mark the build as "dirty" if there are uncommitted or untracked changes
    git_dirty = 1 if get_git_uncommitted_changes() or get_git_untracked_files() else 0
    return git_dirty