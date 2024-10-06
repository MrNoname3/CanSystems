import os
import shutil
import requests
from git_utils import get_git_hash, get_git_commit_count, get_git_dirty

# Function to convert .elf to .bin
def convert_elf_to_bin(source, target, env):
    # Custom BIN from ELF
    env.Execute(" ".join([
        "$OBJCOPY", "-O", "binary",
        "$BUILD_DIR/${PROGNAME}.elf", "$BUILD_DIR/${PROGNAME}.bin"
    ]))
    print("BIN file created successfully.")
    upload()

def upload():
    # Get the project directory (root of the PlatformIO project)
    project_dir = env.subst("$PROJECT_DIR")

    # Get the build directory (relative to project_dir)
    build_dir = env.subst("$BUILD_DIR").replace(project_dir + os.sep, "")

    # Get the firmware name (relative path)
    firmware_name = env.subst("$PROGNAME") + ".bin"

    # Combine the relative build directory and firmware name
    firmware_relative_path = os.path.join(build_dir, firmware_name)
    print("Firmware relative path:", firmware_relative_path)

    # Get the environment name
    environment_name = env['PIOENV']
    print(f"Environment: {environment_name}")

    # Get related git informations
    git_hash = get_git_hash()
    git_commit_count = get_git_commit_count()
    git_dirty = get_git_dirty()

    if not os.path.exists(firmware_relative_path):
        print("Firmware binary not found!")
        return

    # JSON structure to send with the upload
    json_payload = {
        "environment": environment_name,
        "git_hash": git_hash,
        "git_commit_count": git_commit_count,
        "git_dirty": git_dirty
    }

    # Prepare firmware name
    new_firmware_name = f"{environment_name}_{git_hash}_{git_commit_count}_{git_dirty}.bin"
    print("FW name:", new_firmware_name)

    # Upload URL
    upload_url = "http://127.0.0.1:11880/upload-firmware"

    try:
        # Open the firmware file
        with open(firmware_relative_path, 'rb') as f:
            # Upload file with the filename in headers
            headers = {
                'X-Filename': new_firmware_name,  # Custom header for the filename
                'Content-Type': 'application/octet-stream'  # Specify binary content
            }
            
            # Send the file as binary data (without multipart form)
            response = requests.post(upload_url, data=f, headers=headers)

        # Check for successful upload
        if response.status_code == 200:
            print(f"Successfully uploaded firmware to {upload_url}")
        else:
            print(f"Failed to upload firmware. Status code: {response.status_code}")

    except requests.exceptions.RequestException as e:
        # Handle connection errors and other request-related exceptions
        print(f"An error occurred while trying to upload the firmware: {e}")

Import("env")
# Add post-action to trigger the conversion after the ELF file is built
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(convert_elf_to_bin, "Converting $BUILD_DIR/${PROGNAME}.elf to BIN")
)
