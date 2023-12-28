Import("env")
import subprocess

def get_git_commit_info():
    try:
        # Retrieve the short commit hash
        result_hash = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip()
        env['GIT_COMMIT_HASH'] = int(result_hash.decode('utf-8'), 16)

        # Retrieve the commit count
        result_count = subprocess.check_output(['git', 'rev-list', '--count', 'HEAD']).strip()
        env['GIT_COMMIT_COUNT'] = int(result_count.decode('utf-8'))
    except subprocess.CalledProcessError:
        env['GIT_COMMIT_HASH'] = 0
        env['GIT_COMMIT_COUNT'] = 0

    print("Git commit hash:", hex(env['GIT_COMMIT_HASH']))
    print("Git commit count:", env['GIT_COMMIT_COUNT'])

get_git_commit_info()
