# The image every release-gate group and the coverage run work in: the PlatformIO toolchains,
# installed the way a build installs them, the pinned Python tooling, and what the jobs run with
# besides. CI tags the image with a hash of what decides its contents (scripts/toolchain_key.py),
# and builds it only when the registry has no such tag.

# The runners' job image (gitea-act-runner): Ubuntu with git, zstd, node for pyright and the JavaScript
# actions, and Python 3.14, the newest the pioarduino platform runs on. Its gitea-act-runner.job-image
# label comes along, and is what the runners' cleanup finds superseded copies of this image by.
FROM ghcr.io/mrnoname3/act-ubuntu:26.04@sha256:bdb1dce00dfa9911095b46370cdede6e859c454e81b7d45725b01e0b7a409db8
# g++ for the native tests. The workspace a runner hands a job container is its own user's, and git
# refuses, as root, to work in a repository someone else owns.
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends g++ \
 && rm -rf /var/lib/apt/lists/* \
 && git config --system --add safe.directory '*'
# By path rather than through HOME, which a CI runner may point elsewhere in a job container.
ENV PATH=/opt/pio/bin:$PATH PLATFORMIO_CORE_DIR=/root/.platformio

COPY . /tmp/project
# One step, so that what it removes never lands in a layer. Configuring an environment installs what
# it builds with, and `pio project metadata` configures every one in file order: the ESP32 ones before
# the ESP8266 ones that name its esptool, and check_esp32, whose configuration has the pioarduino
# platform install clang-tidy. Left out afterwards: the download and install staging, and what only
# another ESP chip or picolibc uses.
RUN python3 -m venv /opt/pio \
 && /opt/pio/bin/pip install --no-cache-dir -r /tmp/project/requirements-ci.txt -r /tmp/project/requirements-dev.txt \
 && cd /tmp/project \
 && pio project metadata > /dev/null \
 && test -x /root/.platformio/packages/tool-clangtidy/clang-tidy \
 && test -d /root/.platformio/packages/tool-esptoolpy/esptool \
 && cd /root/.platformio \
 && rm -rf .cache dist tools packages/toolchain-xtensa-esp-elf/picolibc /tmp/* \
 && find packages/framework-arduinoespressif32-libs packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/lib \
      -mindepth 1 -maxdepth 1 -type d -name 'esp32?*' -exec rm -rf {} +
