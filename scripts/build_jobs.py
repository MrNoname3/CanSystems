"""How many compilers `pio run` runs at once: as many as the cgroup's CPU quota grants.

SCons starts one compiler per CPU the kernel reports. A container capped to fewer CPUs than the
machine has would run them all inside its own CPU and memory limits, so it gets `--jobs` set to
its quota instead. Without a quota, the default stays.
"""

import math
import os
from pathlib import Path

CPU_MAX = Path("/sys/fs/cgroup/cpu.max")
MEMORY_MAX = Path("/sys/fs/cgroup/memory.max")
MEMORY_CURRENT = Path("/sys/fs/cgroup/memory.current")


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


def analysis_jobs(cpu_max: Path = CPU_MAX, memory_max: Path = MEMORY_MAX,
                  memory_current: Path = MEMORY_CURRENT) -> int:
    """At most four tidy processes, one per whole CPU and per GiB of available memory.

    A 1.5-CPU container deliberately gets one worker. The remainder leaves room for the runner
    and log processing; compiler builds may still use quota_jobs()'s rounded-up count.
    """
    cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count() or 1
    try:
        quota, period = cpu_max.read_text(encoding="ascii").split()[:2]
        if quota != "max":
            cpus = min(cpus, max(1, int(quota) // int(period)))
    except (OSError, ValueError, ZeroDivisionError):
        pass
    memory_jobs = 1                         # unknown memory limits get the conservative default
    try:
        available = next(int(line.split()[1]) * 1024 for line in
                         Path("/proc/meminfo").read_text().splitlines() if line.startswith("MemAvailable:"))
        if memory_max.exists() and memory_max.read_text().strip() != "max":
            available = min(available, int(memory_max.read_text()) - int(memory_current.read_text()))
        memory_jobs = max(1, (available - 512 * 1024 ** 2) // (1024 ** 3))
    except (OSError, ValueError, StopIteration):
        pass
    return max(1, min(4, cpus, memory_jobs))
