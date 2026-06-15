import os
import re

Import("env")

def patch_elf2bin():
    # Get the ESP8266 framework directory path
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif8266")

    if not framework_dir:
        print("ESP8266 framework directory not found, skipping elf2bin.py patch.")
        return

    # Define the path to elf2bin.py
    elf2bin_path = os.path.join(framework_dir, "tools", "elf2bin.py")

    if not os.path.exists(elf2bin_path):
        print(f"elf2bin.py not found at {elf2bin_path}, skipping patch.")
        return

    # Read the file
    with open(elf2bin_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Check if patch needs to be applied
    if r"re.split(r'\s+', line)" not in content:
        print("Applying patch to elf2bin.py...")

        # Apply the patch - fix invalid escape sequences
        content = content.replace("re.split('\\s+', line)", "re.split(r'\\s+', line)")

        # Write the patched content back
        with open(elf2bin_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"Successfully patched {elf2bin_path}")

patch_elf2bin()
