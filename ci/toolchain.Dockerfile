# The PlatformIO toolchains the release-gate groups build and analyse with, installed the way a build
# installs them, so a CI job starts with them in place. CI tags the image with a hash of this file,
# platformio.ini and requirements-ci.txt, and builds it only when the registry has no such tag.

FROM ghcr.io/catthehacker/ubuntu:act-24.04@sha256:dff4ec57d90046a7283aafc314298380be82bfeccb9ad0f1b36c4ebe74aabe78 AS base

FROM base AS install
COPY requirements-ci.txt /opt/pio/requirements-ci.txt
RUN python3 -m venv /opt/pio && /opt/pio/bin/pip install --no-cache-dir -r /opt/pio/requirements-ci.txt
COPY . /tmp/project
# Configuring an environment installs what it builds with, and `pio project metadata` configures
# every one in file order: the ESP32 ones before the ESP8266 ones that name its esptool, and
# check_esp32, whose configuration has the pioarduino platform install clang-tidy.
RUN cd /tmp/project \
 && /opt/pio/bin/pio project metadata > /dev/null \
 && test -x /root/.platformio/packages/tool-clangtidy/clang-tidy \
 && test -d /root/.platformio/packages/tool-esptoolpy/esptool
# Left out: the download and install staging, and what only another ESP chip or picolibc uses.
RUN cd /root/.platformio \
 && rm -rf .cache dist tools packages/toolchain-xtensa-esp-elf/picolibc \
 && find packages/framework-arduinoespressif32-libs packages/toolchain-xtensa-esp-elf/xtensa-esp-elf/lib \
      -mindepth 1 -maxdepth 1 -type d -name 'esp32?*' -exec rm -rf {} +

FROM base
COPY --from=install /opt/pio /opt/pio
COPY --from=install /root/.platformio /root/.platformio
# By path rather than through HOME, which a CI runner may point elsewhere in a job container.
ENV PATH=/opt/pio/bin:$PATH PLATFORMIO_CORE_DIR=/root/.platformio
