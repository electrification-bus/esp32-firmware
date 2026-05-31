# Getting Started — CLI / terminal workflow

This is the command-line counterpart to [`GettingStarted_VSCode.md`](GettingStarted_VSCode.md). If you're using VSCode + the PlatformIO extension, follow that doc instead — this one assumes you want to build, flash, and monitor from a terminal (Emacs, vim, etc. as the editor; iTerm/Terminal.app as the surface).

## What's in this workflow

- A small **`./bin/pio` shim** that points at PlatformIO Core installed under `~/.platformio/penv/`. No `$PATH` edits required, no fresh PIO install — if the VSCode PlatformIO extension is installed, the shim already works.
- A **root `Makefile`** with short targets (`make build` / `flash` / `fs` / `uploadfs` / `flash-all` / `monitor` / `clean` / `devices` / `envs` / `check` / `config-*`). Default board is `esp32-poe-iso` (the Olimex POE-ISO); overridable per-command with `PIO_ENV=...`.
- A **per-developer config overlay** at `~/.config/eBus/config.yml` (gitignored, never committed). The repo carries `data/config.yml.example` as the schema + safe-defaults template; the staged build file `data/config.yml` is generated from the overlay just-in-time before any LittleFS-image target.

## Prerequisites

- macOS / Linux with `make`, `python3`, `git`, and a shell (`zsh` / `bash`).
- **PlatformIO Core** installed somewhere `./bin/pio` can find. The default is `~/.platformio/penv/bin/pio` — the path the VSCode PlatformIO extension uses. If you don't have it, install via any of: VSCode + PlatformIO IDE extension, `pipx install platformio`, or `brew install platformio`. Then symlink to the expected location if needed.
- An ESP32 board with a USB-to-serial connection. The default `PIO_ENV=esp32-poe-iso` targets the Olimex ESP32-POE-ISO (CH340 USB-serial, enumerates as `/dev/cu.usbserial-*`). Other supported envs live in `platformio.ini`; list them with `make envs`.

## First-time bootstrap (after a fresh clone)

```sh
git clone <repo-url>
cd esp32-sdk

# Verify the PlatformIO shim works
./bin/pio --version          # expect: PlatformIO Core, version 6.x

# Bootstrap your personal config overlay from the schema example.
# Creates ~/.config/eBus/config.yml if missing; no-op if present.
make config-init

# Edit it with your MQTT broker, credentials, device id, etc.
# (The defaults point at the public Shiftr.io test broker for first-boot
# smoke testing — replace with your real broker for any actual use.)
$EDITOR ~/.config/eBus/config.yml

# Sanity-check
make config-status
make envs
make devices
```

`make config-init` is idempotent — running it a second time will tell you the overlay already exists and refuse to clobber. `make config-status` shows the overlay path, whether it's present, and whether `data/config.yml` is in sync.

## The build / flash / monitor loop

```sh
make build         # compile firmware
make flash         # upload firmware over USB
make fs            # build LittleFS image from data/ (config overlay staged automatically)
make uploadfs      # upload LittleFS image over USB (overlay staged first)
make flash-all     # upload firmware + filesystem in one shot
make monitor       # open serial monitor — must be in a REAL terminal (see below)
make clean         # clean build artifacts for the current env
```

All targets default to `PIO_ENV=esp32-poe-iso`. Override per-invocation:

```sh
make build PIO_ENV=esp32-s3-devkitc-1
make flash PORT=/dev/cu.usbserial-XXXXX     # override auto-detected serial port
```

`make help` lists everything with one-line descriptions; `make envs` lists every `[env:*]` defined in `platformio.ini`.

> **Why `PIO_ENV` and not `ENV`?** On macOS, the shell's `ENV` variable is commonly preset to `~/.kshrc` (legacy ksh init-file convention). A Makefile that wrote `ENV ?= esp32-poe-iso` would have its default silently overridden. `PIO_ENV` avoids the collision.

## Serial monitor — needs a real terminal

`make monitor` runs `pio device monitor`, which is interactive (it puts the controlling terminal into raw mode to capture single keystrokes). It **requires a real PTY**: a true terminal window like Terminal.app / iTerm / a tmux pane.

It will *not* work if launched from:
- A non-interactive `bash -c '...'` invocation
- Output-capturing harnesses (CI pipelines, scripted automation)
- Claude Code's `!` shell-prefix in this session

If `make monitor` errors out with `termios.error: Operation not supported on socket` (errno 102) or `Operation not supported by device` (errno 19), that's the symptom — open a real terminal window and run it there instead. Useful keys once inside miniterm: `Ctrl-T h` for the menu, `Ctrl-T q` to quit cleanly (releases the port).

For *read-only* serial capture that works anywhere — e.g. for piping into a log file — use raw `cat` after `stty` setup:

```sh
stty -f /dev/cu.usbserial-21130 115200 cs8 -cstopb -parenb raw -echo
cat /dev/cu.usbserial-21130 | tee /tmp/serial.log
```

## Config overlays — see Configuration.md

Two overlay files keep per-developer settings (MQTT credentials, upload-speed tweaks, etc.) out of the shared repo: `~/.config/eBus/config.yml` for firmware runtime config and `platformio_local.ini` for PlatformIO build/upload tweaks. The full model — flows, bootstrap commands, schemas, common overrides — lives in [`Configuration.md`](Configuration.md). Read that doc once after `make config-init` to understand both overlays end-to-end.

The CLI workflow above (`make fs`, `make flash-all`, etc.) all run through the overlay layer automatically. You don't need to think about it during ordinary development — only during initial setup or when something drifts.

For the long-term plan to move credentials out of LittleFS entirely and into NVS (runtime-provisioned via captive portal), see [`proposal-runtime-config-via-nvs.md`](proposal-runtime-config-via-nvs.md) — that's a design note awaiting discussion with Doug.

## Emacs integration

`pio` output is plain GCC-formatted compiler diagnostics (`file:line: warning: …`), which Emacs `compile-mode` parses out of the box. Workflow:

- **Build with error navigation**: `M-x compile` → `make build` → `next-error` (`C-x ` `` ` ``) jumps to each warning/error in the source.
- **Quick rebuild**: `M-x recompile` re-runs the last compile command without re-prompting.
- **Filesystem builds**: `M-x compile` → `make fs` (or `make uploadfs` if you want it to upload too). The config overlay is staged automatically.
- **Serial monitor**: do *not* use `M-x serial-term` for this — it pre-dates miniterm's bidirectional handling and doesn't deal cleanly with the CH340 chip's reset toggling. Open a separate terminal (or tmux pane) and run `make monitor` there. If you want the output to also appear in Emacs, run `make monitor 2>&1 | tee /tmp/serial.log` in the terminal, then `M-x auto-revert-tail-mode` on `/tmp/serial.log` from Emacs.
- **`compile-error-regexp-alist`**: usually no customization needed — pio's GCC output is recognized by Emacs's default GCC regexp.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `make: pio: command not found` | `./bin/pio` shim is missing executable bit, OR `~/.platformio/penv/bin/pio` doesn't exist | `chmod +x bin/pio`, then install PlatformIO via the VSCode extension or `pipx install platformio` |
| `make build` defaults to wrong env (e.g. shows `PIO_ENV=/Users/...` in help) | Shell has `ENV` set (collision); Makefile uses `PIO_ENV` to avoid this, so use that | Use `make build PIO_ENV=...` explicitly; check `printenv | grep PIO_ENV` |
| `make flash` finds no port / hangs at "Connecting…" | No board attached, wrong port, or board in non-bootloader state | `make devices` to list ports; some boards need BOOT held during reset for first flash |
| `make monitor` → `termios.error` | Running through a non-PTY context (pipe, harness, Claude Code `!` prefix) | Run in a real terminal window |
| `make fs` fails with "config overlay missing" | First-run state; no `~/.config/eBus/config.yml` yet | `make config-init` to bootstrap from the example, then edit |
| `data/config.yml` showed up modified after pulling | Someone else's overlay snuck in via a commit (shouldn't happen — `data/config.yml` is gitignored) | `git checkout data/config.yml` to discard, then `make config-stage` to re-stage from overlay |
| OTA (over network) flash | Not yet wired into the Makefile — this firmware does HTTP OTA via the captive portal, not ArduinoOTA | Use the captive portal's web UI on port 80 for now; see commit `04e1fa9` for context |

## Reference

- [`Makefile`](../Makefile) — the canonical source of truth for all targets; `make help` regenerates from it
- [`bin/pio`](../bin/pio) — the PlatformIO Core shim
- [`PlatformIO_commands.txt`](PlatformIO_commands.txt) — Doug's raw-command cheatsheet, no narrative; useful when you want to invoke `pio` directly without `make`
- [`proposal-runtime-config-via-nvs.md`](proposal-runtime-config-via-nvs.md) — design note for the future NVS-backed runtime credential model
