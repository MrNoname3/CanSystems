# CLAUDE.md

Working notes for this repo. **README.md** is the source of truth for the architecture,
MQTT scheme, OTA flow, on-device config, and repository layout — read it for the "what".
This file covers the "how": exact commands in this environment, and the conventions to
follow when editing.

## Commands

PlatformIO is not on `PATH` here, and a project `.venv` confuses pio's virtualenv detection, so
**always run pio as:**

```sh
VIRTUAL_ENV="" ~/.platformio/penv/bin/pio <args>
```

- Build all envs: `… pio run` · one env: `… pio run -e <env>`
- Native tests: `… pio test -e native_test` (under a minute)
- Static analysis: `… pio check` (cppcheck + clang-tidy; checks live in `.clang-tidy`)
- **Release gate** (build + test + check + format + lint + typecheck + pytest, fail-fast):
  `python scripts/release_check.py` (`--strict` fails on a dirty tree, `--sync` refreshes .venv)
- Individual guards: `scripts/deps_check.py` (the .venv - or CI's own Python - matches the
  pins; `--sync` installs them),
  `scripts/format_check.py` (clang-format + final newline),
  `scripts/lint_check.py` (ruff), `scripts/typecheck_check.py` (pyright strict), `scripts/pytest_check.py`
- Python tooling (clang-format/ruff/pyright/pytest/gcovr) is pinned in `requirements-dev.txt`;
  install it into a **project-root `.venv`** (`python -m venv .venv && .venv/bin/pip install
  -r requirements-dev.txt`) — every gate guard finds it there. pio is unaffected by the root
  `.venv` (it runs from its own penv). `ota/requirements.txt` holds only the OTA tool's runtime
  deps, and `requirements-ci.txt` the two CI-only ones (platformio, intelhex). All three are
  pinned exact.
- urboot bootloader: `scripts/build_urboot.sh [771|800|801]` (podman/docker; see `bootloader/README.md`)

The build must stay **warning-clean under `-Wall -Wextra -Werror`** — keep it that way.

## Editing conventions

- **Explicit types**, not `auto` (modernize-use-auto is deliberately suppressed).
- **English** comments and identifiers only — even when the conversation is in Hungarian.
- Preserve **manually column-aligned trailing comments**: clang-format runs with
  `AlignTrailingComments: Leave`, and `ruff format` is **never** run (it would collapse them) —
  ruff is lint-only.
- Don't rename loop variables (e.g. `i`).
- Match the surrounding file's style and idiom.

## Git workflow

- Always merge with `git merge --no-ff` (never fast-forward).
- Build features on a branch; merge to `master` only when done, reviewed, and CI-green, then
  delete the branch (local + remote) and push `master` to `origin` (= the self-hosted Gitea,
  which **push-mirrors to GitHub** automatically — no second remote needed).
- CI runs the release gate plus non-blocking firmware size-diff and native-coverage jobs, and a
  weekly PlatformIO outdated report. **Gitea Actions and GitHub Actions both run the same
  workflow files** - Gitea scans `.github/workflows` too - and they must stay that way: no
  runner-specific steps. The one exception is `actions/cache`, skipped off github.com because the
  self-hosted runner's cache API is v1 while `actions/cache@v6` speaks v2; that only costs a cold
  (slower) run.
- Dependency bumps come from **Renovate on the Gitea side** (`renovate.json`), which covers the
  pip and GitHub Actions ecosystems, the urboot build image (`bootloader/urboot.Dockerfile`), and
  the `atmelavr` platform pin through a custom manager. Patch/pin/digest automerge after a 3-day
  soak (plus minor for Actions); everything else waits on the dashboard. The urboot image is
  excluded from automerge - the bootloader `.hex` files are committed, so a base-image change
  wants a rebuild and a diff (`URBOOT_OUT_DIR=/tmp/x scripts/build_urboot.sh`). Dependabot is deliberately not used: it only runs on GitHub,
  and GitHub here is a push mirror, so its PRs would land where they cannot be merged. PlatformIO
  pins in `platformio.ini` stay manual.
- End commit messages with the `Co-Authored-By` trailer.

## Dependencies

All PlatformIO platform/package/lib deps are pinned **exact** in `platformio.ini`; keep them
pinned when bumping. **Do not `pip install` into the pio penv** (`~/.platformio/penv`) — it has
broken the ESP32 build before; use the project-root `.venv` (from `requirements-dev.txt`) for
Python tooling.

## Testing notes

- Native suites are in `test/test_*/`, with fakes in `test/_shims/` (Arduino, LittleFS, Update,
  PubSubClient, connectivity, …). Prefer the shim fake clock (`setFakeMillis`) over real `sleep()`
  in time-driven tests.
- **AVR `int` is 16-bit** (ESP `int` is 32-bit): shifts past bit 15 need
  `static_cast<uint32_t>(1) << i`; native tests run on the host and won't catch this.

## Gotchas

See README.md "Gotchas" (cross-project `binId` reflash, CAN IDs in EEPROM). Also:
per-deployment secrets live in the git-ignored `ota/secrets.yaml` — never
commit or print its contents (the git-ignored `ota/mosq-ca.crt` is just the public CA bundle,
regenerated from the system trust store when missing).
