Import("env")
import subprocess

def get_git_commit_hash():
    try:
        result = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip()
        env['GIT_COMMIT'] = result.decode('utf-8')
    except subprocess.CalledProcessError:
        env['GIT_COMMIT'] = "UNKNOWN"
    print("Git commit hash:", env['GIT_COMMIT'])

get_git_commit_hash()
