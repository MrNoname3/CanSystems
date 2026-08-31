"""Turns off one warning for a single Arduino AVR core source file.

The core's `new.cpp` names parameters it never uses - the `nothrow` tag, and the size the C++14
sized deallocation functions are handed - which this project's `-Wall -Wextra -Werror` rejects.
That is upstream's code rather than ours, so the warning is disabled for that one file instead of
for the whole project, and the downloaded toolchain is left exactly as PlatformIO installed it.

The narrow pattern is deliberate: the platform is pinned exact, so if a later version makes
another core file warn, the build should say so rather than silently widen.
"""

Import("env")


def relax_unused_parameter(node):
    """Rebuilds one source node with -Wno-unused-parameter appended."""
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-Wno-unused-parameter"])


env.AddBuildMiddleware(relax_unused_parameter, "*/cores/arduino/new.cpp")
