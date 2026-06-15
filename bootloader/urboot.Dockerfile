# Reproducible, self-contained build image for the urboot bootloader.
#
# Unlike upstream's Dockerfile (which expects the urboot source bind-mounted at
# /src), this one clones urboot at a PINNED tag straight INTO the image, so the
# build needs no host clone and no bind-mount — scripts/build_urboot.sh feeds this
# file to `podman|docker build` on stdin and streams the resulting .hex back out.
# The avr toolchain ships inside the urboot source tree (src/avr-toolchain), so the
# clone brings the exact, version-matched compiler along (hence --platform amd64).
#
# Build arg:  URBOOT_REF   urboot git tag, e.g. u7.7.1 / u8.0 / u8.0.1
FROM --platform=linux/amd64 ubuntu:jammy

# build-essential + perl: urboot's Makefile/helper scripts. Capture::Tiny and
# Number::Range are the only non-core Perl modules upstream installs via cpan
# (the others are core); apt is faster and the Perl layer never affects the
# emitted bytes (it only drives naming / AUTOFRILLS selection).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential perl git ca-certificates \
        libcapture-tiny-perl libnumber-range-perl \
    && rm -rf /var/lib/apt/lists/*

# URBOOT_REPO defaults to a fork as insurance against upstream changes/deletion;
# override with --build-arg URBOOT_REPO=https://github.com/stefanrueger/urboot to
# cross-check against upstream (same tags -> same SHAs -> byte-identical output).
ARG URBOOT_REPO=https://github.com/MrNoname3/urboot
ARG URBOOT_REF
RUN git clone --depth 1 --branch "${URBOOT_REF}" "${URBOOT_REPO}" /urboot

WORKDIR /urboot/src
ENTRYPOINT ["/usr/bin/make"]
