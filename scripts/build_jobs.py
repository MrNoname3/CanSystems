"""How many compilers `pio run` runs at once: as many as the cgroup's CPU quota grants.

SCons starts one compiler per CPU the kernel reports. A container capped to fewer CPUs than the
machine has would run them all inside its own CPU and memory limits, so it gets `--jobs` set to
its quota instead. Without a quota, the default stays.
"""

import math
from pathlib import Path

CPU_MAX = Path("/sys/fs/cgroup/cpu.max")


def quota_jobs(cpu_max: Path = CPU_MAX) -> int | None:
    """The CPUs the cgroup's quota grants, rounded up; None when it sets no quota."""
    try:
        quota, period = cpu_max.read_text(encoding="ascii").split()[:2]
        return None if quota == "max" else max(1, math.ceil(int(quota) / int(period)))
    except (OSError, ValueError):
        return None


def jobs_args(cpu_max: Path = CPU_MAX) -> list[str]:
    """The `pio run` arguments that hold it to the quota, if there is one."""
    jobs = quota_jobs(cpu_max)
    return [] if jobs is None else ["--jobs", str(jobs)]
