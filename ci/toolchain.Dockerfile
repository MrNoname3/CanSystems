# The image every release-gate group and the coverage run work in: the PlatformIO toolchains,
# installed the way a build installs them, the pinned Python tooling, and what the jobs run with
# besides. CI tags the image with a hash of what decides its contents (scripts/toolchain_key.py),
# and builds it only when the registry has no such tag.

FROM node:24.21.0-trixie-slim@sha256:173f125896c3b47ddf056734c7ea789d04595a6a08769a8f78e0df642781fb66 AS node

FROM python:3.14.8-slim-trixie@sha256:fbecd461fc72d21b2143c6eaba25143738079154a6d0ea3da3aec724f50eed4a
# git for the checkout and the version stamp, g++ for the native tests, zstd for actions/cache. The
# workspace a runner hands a job container is its own user's, and git refuses, as root, to work in a
# repository someone else owns.
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates g++ git zstd \
 && rm -rf /var/lib/apt/lists/* \
 && git config --system --add safe.directory '*'
# Node for pyright, and for the JavaScript actions: a Gitea runner runs them with the job
# container's node, where GitHub brings its own.
COPY --from=node /usr/local/bin/node /usr/local/bin/node
# By path rather than through HOME, which a CI runner may point elsewhere in a job container.
ENV PATH=/opt/pio/bin:$PATH PLATFORMIO_CORE_DIR=/root/.platformio

COPY . /tmp/project
# One step, so that what it removes never lands in a layer. Configuring an environment installs what
# it builds with, and `pio project metadata` configures every one in file order: the ESP32 ones before
# the ESP8266 ones that name its esptool, and check_esp32, whose configuration has the pioarduino
# platform install clang-tidy. Left out afterwards: the download and install staging, and what only
# another ESP chip or picolibc uses.
RUN python -m venv /opt/pio \
 && /opt/pio/bin/pip install --no-cache-dir -r /tmp/project/requirements-ci.txt -r /tmp/project/requirements-dev.txt \
 && cd /tmp/project \
 && pio project metadata > /dev/null \
 && test -x /root/.platformio/packages/tool-clangtidy/clang-tidy \
 && test -d /root/.platformio/packages/tool-esptoolpy/esptool \
 && cd /root/.platformio \
 && rm -rf .cache dist tools packages/toolchain-xtensa-esp-elf/picolibc /tmp/* \
 && find packages/framework-arduinoespressif32-libs packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/lib \
      -mindepth 1 -maxdepth 1 -type d -name 'esp32?*' -exec rm -rf {} +
