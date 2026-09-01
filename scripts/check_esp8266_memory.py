"""Reports the ESP8266's two RAM budgets after a build, and fails when either runs thin.

PlatformIO's summary shows neither: its `RAM:` line is static DRAM only, and IRAM is folded into
`Flash:`. Nor does either budget fail in time on its own - IRAM overflows as a link error once it
is already full, and static DRAM never fails at all. It quietly takes bytes off the heap, and the
heap's worst moment is the BearSSL handshake: measured on the thermo firmware, 34268 bytes of
static DRAM leave a largest free block of 10040 bytes while the TLS session is up. The DRAM cap
below keeps about 4 KB of that in reserve.
"""

import re
import subprocess

Import("env")

# IRAM holds code; DRAM holds static data and, in whatever is left, the heap.
IRAM_SECTIONS = (".text", ".text1")
DRAM_SECTIONS = (".data", ".rodata", ".bss", ".noinit")
DRAM_TOTAL = 81920                                        # 0x3FFE8000 - 0x3FFFC000.
IRAM_TOTAL = 32768                                        # 0x40100000 - 0x40108000 by default.
IRAM_TOTAL_MMU_CACHE16 = 49152                            # What PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48 makes of it.
MMU_CACHE16_FLAG = "PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48"

MIN_FREE_IRAM = 1024
MAX_STATIC_DRAM = 40960

# objdump -h lays a section out as: index, name, size, VMA, LMA, file offset, alignment.
#     0 .data         00000680  3ffe8000  3ffe8000  00000094  2**3
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


def iram_budget():
    """The IRAM window the MMU configuration leaves for code."""
    defines = []
    for define in env.get("CPPDEFINES", []):
        defines.append(define[0] if isinstance(define, (list, tuple)) else define)
    return IRAM_TOTAL_MMU_CACHE16 if MMU_CACHE16_FLAG in defines else IRAM_TOTAL


def check_memory(source, target, env):
    del source, target
    elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    objdump = env.subst("$OBJDUMP") or env.subst("$CC").replace("-gcc", "-objdump")

    sizes = section_sizes(objdump, elf)
    iram_total = iram_budget()
    iram = sum(sizes.get(name, 0) for name in IRAM_SECTIONS)
    dram = sum(sizes.get(name, 0) for name in DRAM_SECTIONS)

    problems = []
    if (iram_total - iram) < MIN_FREE_IRAM:
        problems.append(
            f"IRAM is down to {iram_total - iram} free bytes of {iram_total}; below {MIN_FREE_IRAM} "
            f"the next IRAM_ATTR function fails the link"
        )
    if dram > MAX_STATIC_DRAM:
        problems.append(
            f"static DRAM is {dram} bytes, over the {MAX_STATIC_DRAM} cap; every byte here comes off "
            f"the heap that BearSSL needs while a TLS session is up"
        )

    if problems:
        print("\nESP8266 memory check FAILED:")
        for problem in problems:
            print(f"  - {problem}")
        print("  See scripts/check_esp8266_memory.py for what the two budgets are.\n")
        env.Exit(1)

    print(f"ESP8266 memory: IRAM {iram}/{iram_total} ({iram_total - iram} free), "
          f"static DRAM {dram}/{DRAM_TOTAL} ({DRAM_TOTAL - dram} left for the heap)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.VerboseAction(
    check_memory, "Checking the ESP8266 memory budgets in $BUILD_DIR/${PROGNAME}.elf"))
