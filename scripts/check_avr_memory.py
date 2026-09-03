"""Reports the ATmega328P's RAM budget after a build, and fails when it runs thin.

Nothing else does. The linker places `.data` and `.bss` without ever comparing them to the
part's 2 KB of SRAM, and whatever is left over silently becomes the stack's and the heap's to
share - so a firmware that has outgrown the room its stack needs links clean and then corrupts
itself at run time. PlatformIO's `RAM:` line reports the same number but never fails on it.

The cap is a ratchet against growth rather than a measured worst-case stack depth: it keeps
MIN_FREE_RAM bytes clear. The heap side of that is small and known - the WS2812 driver mallocs
its pixel buffer (NeoAvrMethod.h), and SoftwareSerial's receive buffer is static, so it is
already counted below - which leaves the reserve to the stack.
"""

import re
import subprocess

Import("env")

# The statically placed RAM. `.noinit` is RAM as well; it only escapes the clearing in `.init4`.
RAM_SECTIONS = (".data", ".bss", ".noinit")
SRAM_TOTAL = 2048                                         # ATmega328P.

MIN_FREE_RAM = 640

# objdump -h lays a section out as: index, name, size, VMA, LMA, file offset, alignment.
#   0 .data         000001a4  00800100  00004914  000049a8  2**0
SECTION_LINE = re.compile(r"^\s*\d+\s+(\.\S+)\s+([0-9a-f]+)\s+[0-9a-f]+\s")


def section_sizes(objdump, elf):
    """Return {section name: size in bytes} for the ELF."""
    dump = subprocess.run([objdump, "-h", elf], capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in dump.splitlines():
        match = SECTION_LINE.match(line)
        if match is not None:
            sizes[match.group(1)] = int(match.group(2), 16)
    return sizes


def check_memory(source, target, env):
    del source, target
    elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    objdump = env.subst("$OBJDUMP") or env.subst("$CC").replace("-gcc", "-objdump")

    sizes = section_sizes(objdump, elf)
    static_ram = sum(sizes.get(name, 0) for name in RAM_SECTIONS)
    free_ram = SRAM_TOTAL - static_ram

    if free_ram < MIN_FREE_RAM:
        print("\nAVR memory check FAILED:")
        print(
            f"  - static RAM is {static_ram} bytes of {SRAM_TOTAL}, leaving {free_ram} for the "
            f"stack and the heap; below {MIN_FREE_RAM} there is no room left to grow into"
        )
        print("  See scripts/check_avr_memory.py for what the budget covers.\n")
        env.Exit(1)

    print(f"AVR memory: static RAM {static_ram}/{SRAM_TOTAL} "
          f"({free_ram} left for the stack and the heap)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.VerboseAction(
    check_memory, "Checking the AVR RAM budget in $BUILD_DIR/${PROGNAME}.elf"))
