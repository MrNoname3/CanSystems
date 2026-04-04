#!/usr/bin/env python3
"""
size_compare.py – Compare flash/RAM sizes between the current working tree and a given commit.

For each environment, runs 'pio run', parses the flash and RAM usage from PlatformIO's
output, then prints the difference.

Usage:
  python scripts/size_compare.py --commit HASH [--env ENV [ENV ...]]
"""
import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass


_SIZE_RE = re.compile(
    r"(Flash|RAM):\s+\[.*?\]\s+[\d.]+%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)",
)


@dataclass
class Sizes:
    flash_used: int
    flash_total: int
    ram_used: int
    ram_total: int


def _find_pio() -> str:
    pio = shutil.which("pio")
    if pio:
        return pio
    fallback = shutil.os.path.expanduser("~/.platformio/penv/bin/pio")
    if shutil.os.path.exists(fallback):
        return fallback
    raise FileNotFoundError(
        "Could not find 'pio'. Activate the PlatformIO virtual environment "
        "or add ~/.platformio/penv/bin to PATH."
    )


def _get_current_ref() -> str:
    result = subprocess.run(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        capture_output=True, text=True,
    )
    ref = result.stdout.strip()
    if ref == "HEAD":
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True, text=True,
        )
        ref = result.stdout.strip()
    return ref


def _checkout(ref: str) -> bool:
    result = subprocess.run(["git", "checkout", ref], capture_output=True)
    return result.returncode == 0


def _build_and_parse(pio: str, envs: list[str]) -> dict[str, Sizes]:
    cmd = [pio, "run"]
    for env in envs:
        cmd += ["-e", env]

    result = subprocess.run(cmd, capture_output=False, text=True)
    if result.returncode != 0:
        print("  ❌ Build failed.", file=sys.stderr)
        return {}

    return _parse_output_live(pio, envs)


def _build_and_capture(pio: str, envs: list[str]) -> dict[str, Sizes]:
    """Run pio and capture output while also printing it, then parse sizes."""
    cmd = [pio, "run"]
    for env in envs:
        cmd += ["-e", env]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        print(line, end="")
        lines.append(line)
    proc.wait()

    if proc.returncode != 0:
        print("  ❌ Build failed.", file=sys.stderr)
        return {}

    return _parse_lines(lines)


def _parse_lines(lines: list[str]) -> dict[str, Sizes]:
    """
    Parse flash/RAM from captured PlatformIO output lines.

    PlatformIO groups size info per environment. We track the current env from
    'Building in ... mode (env: NAME)' or 'Processing NAME (...)' headers.
    """
    current_env: str | None = None
    env_data: dict[str, dict[str, int]] = {}

    env_header_re = re.compile(r"Processing\s+(\S+)\s*\(")
    building_re = re.compile(r"Building in \S+ mode \(env:([^)]+)\)")

    for line in lines:
        m = env_header_re.search(line)
        if m:
            current_env = m.group(1).strip()
            env_data.setdefault(current_env, {})
            continue
        m = building_re.search(line)
        if m:
            current_env = m.group(1).strip()
            env_data.setdefault(current_env, {})
            continue

        m = _SIZE_RE.search(line)
        if m and current_env is not None:
            kind = m.group(1)          # "Flash" or "RAM"
            used = int(m.group(2))
            total = int(m.group(3))
            env_data[current_env][f"{kind.lower()}_used"] = used
            env_data[current_env][f"{kind.lower()}_total"] = total

    sizes: dict[str, Sizes] = {}
    for env, data in env_data.items():
        if len(data) == 4:
            sizes[env] = Sizes(
                flash_used=data["flash_used"],
                flash_total=data["flash_total"],
                ram_used=data["ram_used"],
                ram_total=data["ram_total"],
            )
    return sizes


def _fmt_bytes(val: int) -> str:
    return f"{val:,}".replace(",", " ") + " B"


def _fmt_diff(diff: int) -> str:
    if diff == 0:
        return "no change"
    sign = "▲ +" if diff > 0 else "▼ "
    return f"{sign}{abs(diff):,} B".replace(",", " ")


def _print_comparison(
    env_name: str,
    current: Sizes,
    baseline: Sizes,
) -> None:
    sep = "─" * 68
    flash_pct_cur = current.flash_used / current.flash_total * 100 if current.flash_total else 0
    ram_pct_cur = current.ram_used / current.ram_total * 100 if current.ram_total else 0
    flash_pct_base = baseline.flash_used / baseline.flash_total * 100 if baseline.flash_total else 0
    ram_pct_base = baseline.ram_used / baseline.ram_total * 100 if baseline.ram_total else 0

    diff_flash = current.flash_used - baseline.flash_used
    diff_ram = current.ram_used - baseline.ram_used

    print(sep)
    print(f" {env_name}")
    print(f"{'':4}{'':8}{'current':>16}{'baseline':>16}{'diff':>16}")
    print(
        f" Flash  {_fmt_bytes(current.flash_used):>16}"
        f"  {_fmt_bytes(baseline.flash_used):>14}"
        f"  {_fmt_diff(diff_flash):>14}"
        f"  (cur {flash_pct_cur:.1f}% / base {flash_pct_base:.1f}%)"
    )
    print(
        f" RAM    {_fmt_bytes(current.ram_used):>16}"
        f"  {_fmt_bytes(baseline.ram_used):>14}"
        f"  {_fmt_diff(diff_ram):>14}"
        f"  (cur {ram_pct_cur:.1f}% / base {ram_pct_base:.1f}%)"
    )
    print(sep)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compare flash/RAM sizes between current state and a baseline commit."
    )
    parser.add_argument(
        "--commit", required=True, metavar="HASH",
        help="Baseline commit hash to compare against",
    )
    parser.add_argument(
        "--env", metavar="ENV", nargs="+",
        help="PlatformIO environment(s) to build (default: all default_envs)",
    )
    args = parser.parse_args()

    pio = _find_pio()
    envs: list[str] = args.env or []

    original_ref = _get_current_ref()

    print(f"\n Building current state ({original_ref})...")
    print("═" * 68)
    current_sizes = _build_and_capture(pio, envs)
    if not current_sizes:
        sys.exit(1)

    print(f"\n Checking out baseline ({args.commit})...")
    if not _checkout(args.commit):
        print(f"❌ Could not checkout '{args.commit}'", file=sys.stderr)
        sys.exit(1)

    print(f"\n Building baseline ({args.commit})...")
    print("═" * 68)
    baseline_sizes = _build_and_capture(pio, envs)

    print(f"\n Returning to {original_ref}...")
    _checkout(original_ref)

    if not baseline_sizes:
        print("❌ Baseline build failed; comparison aborted.", file=sys.stderr)
        sys.exit(1)

    all_envs = sorted(set(current_sizes) | set(baseline_sizes))
    if not all_envs:
        print("No size data found in build output.", file=sys.stderr)
        sys.exit(1)

    print(f"\n Size comparison: {original_ref}  vs  {args.commit}\n")
    for env in all_envs:
        if env not in current_sizes:
            print(f" {env}: no size data for current build")
        elif env not in baseline_sizes:
            print(f" {env}: no size data for baseline build")
        else:
            _print_comparison(env, current_sizes[env], baseline_sizes[env])
    print()


if __name__ == "__main__":
    main()
