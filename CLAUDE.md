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
- Native tests: `… pio test -e native_test` (~30 s, ~442 cases)
- Static analysis: `… pio check` (cppcheck + clang-tidy; checks live in `.clang-tidy`)
- **Release gate** (build + test + check + format + lint + typecheck + pytest, fail-fast):
  `python scripts/release_check.py` (`--strict` fails on a dirty tree)
- Individual guards: `scripts/format_check.py` (clang-format + final newline),
  `scripts/lint_check.py` (ruff), `scripts/typecheck_check.py` (pyright strict), `scripts/pytest_check.py`
- Python tooling (clang-format/ruff/pyright/pytest/gcovr) is pinned in `requirements-dev.txt`;
  install it into a **project-root `.venv`** (`python -m venv .venv && .venv/bin/pip install
  -r requirements-dev.txt`) — every gate guard finds it there. pio is unaffected by the root
  `.venv` (it runs from its own penv). `ota/requirements.txt` holds only the OTA tool's runtime deps.
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
  delete the branch (local + remote) and push `master` to **both** remotes
  (`origin` = GitHub, `gitea`).
- GitHub Actions runs the release gate plus non-blocking firmware size-diff and native-coverage
  jobs, and Dependabot opens weekly PRs for GitHub Actions version bumps; **Gitea CI is
  intentionally off**.
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

See README.md "Gotchas" (cross-project `binId` reflash, CAN IDs in EEPROM). Also: the MQ-135
smoke node is WIP and likely to be dropped — don't flag its dead calibration path or missing
tests as defects. Per-deployment secrets live in the git-ignored `ota/secrets.yaml`
(+ `ota/mosq-ca.crt`) — never commit or print their contents.
