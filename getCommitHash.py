import subprocess

def get_commit_hash():
    result = subprocess.run(["git", "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE, text=True)
    return result.stdout.strip()

if __name__ == "__main__":
    print(get_commit_hash())
