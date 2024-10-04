from git_utils import get_git_hash, get_git_commit_count, get_git_dirty
Import("env")

def get_git_info():
    git_hash = get_git_hash()
    git_commit_count = get_git_commit_count()
    git_dirty = get_git_dirty()

    env['GIT_COMMIT_HASH'] = git_hash
    env['GIT_COMMIT_COUNT'] = git_commit_count
    env['GIT_DIRTY'] = git_dirty

    print("Git commit hash:", hex(env['GIT_COMMIT_HASH']))
    print("Git commit count:", env['GIT_COMMIT_COUNT'])
    print("Git dirty:", env['GIT_DIRTY'])

get_git_info()
