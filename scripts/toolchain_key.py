"""The tag of the CI toolchain image: a hash of what decides its contents.

The image carries the PlatformIO packages the environments in platformio.ini install, so the tag
follows only the options that choose them; a build flag, a source filter or a library leaves it
alone. Each environment's options are resolved the way PlatformIO resolves them - its own, then
what it extends, then [env], with ${section.option} expanded - so moving an option between
sections, or adding an environment on a platform already there, does not change the tag either.
An option wrongly ignored here costs time, not correctness: pio installs a package the image
lacks in the job that needs it.
"""

import argparse
import configparser
import fnmatch
import hashlib
import re
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
# What the image is built from besides platformio.ini, which counts through toolchain_config().
IMAGE_INPUTS = ("ci/toolchain.Dockerfile", "requirements-ci.txt", "requirements-dev.txt", "ota/requirements.txt")
# Options that never change which packages PlatformIO installs.
IGNORED = (
    "build_*", "lib_*", "monitor_*", "extends", "extra_scripts",
    "check_flags", "check_patterns", "check_severity", "check_skip_packages", "check_src_filters",
    "debug_init_*", "debug_speed", "test_filter", "test_ignore",
    "upload_flags", "upload_port", "upload_resetmethod", "upload_speed",
    "board_bootloader.*", "board_fuses.*", "board_upload.*", "board_build.f_cpu", "board_build.f_flash",
    "board_build.flash_mode", "board_build.ldscript", "board_build.partitions",
)
REFERENCE = re.compile(r"\$\{([^.}()]+)\.([^}]+)\}")
MAX_DEPTH = 10                                          # a reference cycle stops here, unexpanded


def read_config(ini: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(inline_comment_prefixes=(";",), interpolation=None, strict=False)
    parser.read(ini, encoding="utf-8")
    return parser


def lookup_order(parser: configparser.ConfigParser, section: str) -> list[str]:
    """The sections PlatformIO looks an option of `section` up in: the section itself, then those it
    extends (the last one named first, each followed by what it extends), then [env]."""
    queue = ["env", section] if section.startswith("env:") else [section]
    order: list[str] = []
    while queue:
        name = queue.pop()
        if name in order or not parser.has_section(name):
            continue
        order.append(name)
        if parser.has_option(name, "extends"):
            queue += [base for base in re.split(r"[\s,]+", parser.get(name, "extends")) if base]
    return order


def resolve(parser: configparser.ConfigParser, section: str, option: str, depth: int = 0) -> str | None:
    """The value of `option` in `section` with its references expanded; None when it has none.
    A reference to something platformio.ini does not set (a PlatformIO built-in, an environment
    variable, `this`) stays as written."""
    def expand(match: re.Match[str]) -> str:
        found = resolve(parser, match[1], match[2], depth + 1) if depth < MAX_DEPTH else None
        return match[0] if found is None else found

    for name in lookup_order(parser, section):
        if parser.has_option(name, option):
            return REFERENCE.sub(expand, parser.get(name, option))
    return None


def toolchain_config(ini: Path) -> str:
    """The package-deciding options of the environments, resolved: one block per distinct set."""
    parser = read_config(ini)
    blocks: set[str] = set()
    for section in parser.sections():
        if not section.startswith("env:"):
            continue
        options = {option for name in lookup_order(parser, section) for option in parser.options(name)}
        blocks.add("\n".join(sorted(
            f"{option} = {' '.join((resolve(parser, section, option) or '').split())}"
            for option in options if not any(fnmatch.fnmatchcase(option, pattern) for pattern in IGNORED))))
    return "\n\n".join(sorted(blocks)) + "\n"


def image_tag(project: Path = PROJECT_DIR) -> str:
    digest = hashlib.sha256()
    for name in IMAGE_INPUTS:
        digest.update(f"{name}\0".encode() + (project / name).read_bytes() + b"\0")
    digest.update(toolchain_config(project / "platformio.ini").encode())
    return digest.hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Print the tag of the CI toolchain image.")
    parser.add_argument("--config", action="store_true",
                        help="print the resolved options the tag covers instead of the tag")
    args = parser.parse_args(argv)
    print(toolchain_config(PROJECT_DIR / "platformio.ini").rstrip("\n") if args.config else image_tag())
    return 0


if __name__ == "__main__":
    sys.exit(main())
