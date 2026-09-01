"""Fails the AVR build when the startup capture has been optimised out of the image.

The reset reason survives one very specific arrangement (lib/resetHandler/src/resetHandler.cpp):
a handler placed in `.init3` reads r2 before the compiler can use it, and stores the result in
`.noinit` so `.init4` does not clear it. Every part of that is invisible to the compiler's own
diagnostics, and two ways of losing it have already happened once each:

  - `--gc-sections` drops the handler when nothing references ResetHandler, and the dead store
    lets the optimiser reuse the register holding the r2 read;
  - a variable that ends up in `.bss` instead of `.noinit` is zeroed by `.init4`, so the marker
    never survives the reset it exists to describe.

Both leave a firmware that builds clean, links clean and reports nonsense on the bench, which is
the sort of thing a build has to say out loud.
"""

import re
import subprocess

Import("env")

# Symbols that have to be in the image, and the section each one belongs in.
REQUIRED_IN_NOINIT = ("bootResetFlags", "restartMarker", "restartCause")
REQUIRED_FUNCTION = "captureResetFlags"
# Everything in .init0 - .init3 is linked ahead of __do_copy_data, which is .init4's entry point.
# Comparing addresses is what proves the handler still runs before .data and .bss are set up.
RUNS_BEFORE = "__do_copy_data"


# objdump -t lays a line out as: address, a seven-column flag field that contains spaces, the
# section, the size, then the name - which an optional visibility keyword can precede:
#   0080053c l     O .noinit  00000001 _ZN12_GLOBAL__N_114bootResetFlagsE
#   000001a6 g       .text    00000016 .hidden __do_copy_data
SYMBOL_LINE = re.compile(r"^([0-9a-f]+)\s(.{7})\s(\S+)\s+([0-9a-f]+)\s+(.*)$")


def objdump_symbols(objdump, elf):
    """Return {symbol name: (address, section)} for the ELF's symbol table."""
    out = subprocess.run([objdump, "-t", elf], capture_output=True, text=True, check=True).stdout
    symbols = {}
    for line in out.splitlines():
        match = SYMBOL_LINE.match(line)
        if match is None:
            continue
        address, _flags, section, _size, trailing = match.groups()
        parts = trailing.split()
        if parts:
            symbols[parts[-1]] = (int(address, 16), section)
    return symbols


def find_symbol(symbols, wanted):
    """Look a symbol up by its plain name, ignoring C++ mangling around it."""
    for name, value in symbols.items():
        if name == wanted or wanted in name:
            return name, value
    return None, None


def check_startup_capture(source, target, env):
    del source, target
    elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    objdump = env.subst("$OBJDUMP") or env.subst("$CC").replace("-gcc", "-objdump")

    symbols = objdump_symbols(objdump, elf)
    problems = []

    handler, handler_value = find_symbol(symbols, REQUIRED_FUNCTION)
    if handler is None:
        problems.append(
            f"{REQUIRED_FUNCTION}() is not in the image - the linker dropped it, so the reset "
            f"reason is never captured"
        )

    for wanted in REQUIRED_IN_NOINIT:
        name, value = find_symbol(symbols, wanted)
        if name is None:
            problems.append(f"{wanted} is not in the image")
        elif value[1] != ".noinit":
            problems.append(
                f"{wanted} is in {value[1]}, not .noinit - .init4 will clear it before it can be read"
            )

    _, boundary = find_symbol(symbols, RUNS_BEFORE)
    if boundary is None:
        # Skipping the comparison because the marker went missing would be the very failure this
        # script exists to catch, dressed up as a pass.
        problems.append(f"{RUNS_BEFORE} is not in the symbol table, so the ordering cannot be checked")
    elif handler_value is not None and handler_value[0] >= boundary[0]:
        problems.append(
            f"{REQUIRED_FUNCTION}() is linked at 0x{handler_value[0]:04x}, at or after "
            f"{RUNS_BEFORE} at 0x{boundary[0]:04x} - it no longer runs before .data and .bss"
        )

    if problems:
        print("\nAVR startup check FAILED:")
        for problem in problems:
            print(f"  - {problem}")
        print("  See lib/resetHandler/src/resetHandler.cpp for what this arrangement relies on.\n")
        env.Exit(1)

    print(f"AVR startup check: {REQUIRED_FUNCTION}() runs before .init4, "
          f"{' and '.join(REQUIRED_IN_NOINIT)} are in .noinit")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", env.VerboseAction(
    check_startup_capture, "Checking the AVR startup capture in $BUILD_DIR/${PROGNAME}.elf"))
