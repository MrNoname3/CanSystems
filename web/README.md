# Browser firmware installer

A static page that flashes the ESP boards' released firmware over USB and then shows their serial
output, built on [ESP Web Tools](https://github.com/esphome/esp-web-tools). It needs desktop Chrome
or Edge (Web Serial) and is served from GitHub Pages for each release.

Each board's button updates the firmware only. Leave **Erase device** unchecked when asked: the
configuration on the board's LittleFS then stays.

**Firmware and configuration** sets up a new or erased board instead, as the OTA tool's **Initial
provisioning** does ([`ota/README.md`](../ota/README.md)): the firmware, and a filesystem holding the
`/config` files `devices.yaml` gives that kind of board, filled from the form. It replaces the whole
filesystem. The page builds the image itself, so what the form holds never leaves the browser:

- The release carries each environment's configuration template, its `pio run -t buildfs` image
  with one placeholder file per `/config` path, each owning exactly one block.
- `fspatch.mjs` writes a file into its placeholder's block and the real size into the metadata
  naming it, recomputing that commit's CRC; LittleFS checksums metadata, not data blocks.
- `cansystems-config.json` gives the image's flash offset and block size, the `server.json` rules
  `otaUpdate.py` checks against, and the default CA bundle (ISRG Root X1 + X2), which the release job
  takes from the system trust store as the OTA tool does.

`installer.py assets` fills each template the way the page does and reads it back with LittleFS before
the release is published, so a filesystem layout the patch no longer fits fails the release instead.

## Releases

```sh
python scripts/release_tag.py --dry-run   # which tag, and whether HEAD may be released
python scripts/release_tag.py             # tag the pushed master head and push the tag
```

The tag runs the whole CI, then two more jobs:

- `release` checks that the tag is the commit count of a `master` commit, builds the environments
  the manifests name, and publishes them with a `SHA256SUMS` through `scripts/forge_release.py` -
  on GitHub and on Gitea alike. Older `v<N>` releases are deleted (`--keep`),
  their tags stay.
- `pages` downloads that release, checks it against `SHA256SUMS`, and deploys the page around it.
  Browsers cannot fetch release files from another origin, so the page serves its own copies.
  It only warns where a forge has no Pages.

GitHub Pages has to be set to **Settings → Pages → Source: GitHub Actions** once.

## Layout

- `index.html` - the page; `@VERSION@` becomes the release tag.
- `config.js`, `fspatch.mjs` - the configuration form and the template filling; the tests run
  `fspatch.mjs` under node, and skip it where there is none.
- `manifests/` - one ESP Web Tools manifest per firmware, naming the release files and so deciding
  what a release contains. An ESP8266 is released as `firmware.bin`, `cansystems-<environment>.bin`
  at offset 0. An ESP32 is released as its merged factory image cut around the NVS partition its
  own partition table names: `cansystems-<environment>.boot.bin` at 0 and `cansystems-<environment>.bin`
  where NVS ends. NVS holds the Arduino EEPROM, the CAN ids among it, which the factory image would
  blank.
- `package.json`, `package-lock.json` - the pinned ESP Web Tools version, which Renovate updates;
  `installer.py` copies its browser bundle from the locked tarball after checking its integrity.
- `installer.py` - `envs`, `assets` and `build`, which CI runs and which build the page locally
  (`assets` needs node, and `--pio` where `pio` is not on `PATH`):

```sh
pio run -e project_esp8266_thermo -e project_esp8266_rad -e project_esp32_can
python web/installer.py assets --into .web-cache/release --pio ~/.platformio/penv/bin/pio
python web/installer.py build --firmware .web-cache/release --version local --output .web-cache/site
python -m http.server 8000 --bind localhost --directory .web-cache/site
```

Web Serial works on `localhost` too, so <http://localhost:8000> can flash an attached board.
