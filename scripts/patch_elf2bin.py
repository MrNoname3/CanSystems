"""Fixes an invalid escape sequence in the ESP8266 framework's elf2bin.py tool.

This writes into an installed framework package on every build, which is what `patch_new_lib.py`
was removed for doing to the Arduino AVR core. It stays anyway, and on purpose: without it the
tool still works - same regex result, same exit code - but Python 3.12 and newer print a
SyntaxWarning, and elf2bin.py runs as `__main__`, so nothing is cached and the warning is reprinted
at every link of every ESP8266 environment. The framework is pinned, so no version bump retires it.
Deleting this script, silencing the warning from the build environment and vendoring elf2bin.py
were each weighed and turned down; leave it as it is.
"""

import os

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
    with open(elf2bin_path, encoding='utf-8') as f:
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
