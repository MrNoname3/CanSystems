import os
import glob

Import("env")


def patch_onewire():
    """Fix malformed `#undef` directives in paulstoffregen/OneWire 2.3.8.

    The ESP32 cleanup block at the end of OneWire.cpp mistakenly keeps the macro
    body after `#undef`:

        #  undef noInterrupts() {portMUX_TYPE mux = ...;portENTER_CRITICAL(&mux)
        #  undef interrupts() portEXIT_CRITICAL(&mux);}

    GCC warns ("extra tokens at end of #undef directive"), which -Werror rejects.
    Rewrite them to the intended `#undef noInterrupts` / `#undef interrupts` so the
    library compiles cleanly without relaxing -Werror for the whole environment.
    Idempotent: a no-op once the file is patched (or on a OneWire version without the bug).
    """
    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR")
    pioenv = env.get("PIOENV")
    if not libdeps_dir or not pioenv:
        print("OneWire patch: libdeps dir / env unknown, skipping.")
        return

    sources = glob.glob(os.path.join(libdeps_dir, pioenv, "OneWire", "OneWire.cpp"))
    if not sources:
        print("OneWire patch: OneWire.cpp not found, skipping.")
        return

    bad_no_interrupts = "#  undef noInterrupts() {portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;portENTER_CRITICAL(&mux)"
    bad_interrupts = "#  undef interrupts() portEXIT_CRITICAL(&mux);}"

    for path in sources:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        if bad_no_interrupts in content or bad_interrupts in content:
            print(f"Applying patch to {path} (malformed #undef directives)...")
            content = content.replace(bad_no_interrupts, "#  undef noInterrupts")
            content = content.replace(bad_interrupts, "#  undef interrupts")
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
            print("OneWire.cpp patched successfully.")
        else:
            print(f"OneWire.cpp already patched (or no fix needed): {path}")


patch_onewire()
