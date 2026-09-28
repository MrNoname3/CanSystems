"""Hands the check_* environments' clang-tidy the toolchain headers in the order GCC searches them.

PlatformIO collects a toolchain's include directories with unsorted globs, so their order follows
the file system and differs between machines. This orders them the way the compiler does: the C++
library first, then GCC's own headers, then the C library. The ESP32 toolchain also carries
picolibc next to newlib; the core is built against newlib, so picolibc's headers are left out.
"""

Import("projenv")

dump_includes = projenv.DumpIntegrationIncludes  # an AttributeError, should PlatformIO rename it


def search_rank(directory: str) -> tuple[int, str]:
    if "/include/c++/" in directory:
        return 0, directory
    if "/lib/gcc/" in directory:
        return 1, directory
    return 2, directory


def ordered_includes(_env: object) -> dict[str, list[str]]:
    includes = dump_includes()
    includes["toolchain"] = sorted((directory for directory in includes["toolchain"]
                                    if "/picolibc/" not in directory + "/"), key=search_rank)
    return includes


projenv.AddMethod(ordered_includes, "DumpIntegrationIncludes")
