# Browser firmware installer

A static page that flashes the ESP boards' released firmware over USB and then shows their serial
output, built on [ESP Web Tools](https://github.com/esphome/esp-web-tools). It needs desktop Chrome
or Edge (Web Serial).

It updates the firmware only. Leave **Erase device** unchecked when asked: the configuration on the
board's LittleFS then stays. A new or erased board is configured with the OTA tool's **Initial
provisioning** ([`ota/README.md`](../ota/README.md)).

## Layout

- `index.html` - the page; `@VERSION@` becomes the release tag.
- `manifests/` - one ESP Web Tools manifest per firmware. Each names one release file,
  `cansystems-<environment>.bin` at offset 0, and so decides what a release contains: an ESP32 is
  released as its merged factory image, an ESP8266 as `firmware.bin`.
- `package.json`, `package-lock.json` - the pinned ESP Web Tools version, which Renovate updates;
  `installer.py` copies its browser bundle from the locked tarball after checking its integrity.
- `installer.py` - `envs`, `assets` and `build`, which build the page:

```sh
pio run -e project_esp8266_thermo -e project_esp8266_rad -e project_esp32_can
python web/installer.py assets --into .web-cache/release
python web/installer.py build --firmware .web-cache/release --version local --output .web-cache/site
python -m http.server 8000 --bind localhost --directory .web-cache/site
```

Web Serial works on `localhost` too, so <http://localhost:8000> can flash an attached board.
