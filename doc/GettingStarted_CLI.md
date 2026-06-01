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

## Monitor ↔ flash port handoff (when monitor runs in tmux)

`make monitor` and `make flash` (and `uploadfs`, `flash-all`) both need exclusive access to the serial port — the macOS kernel only grants `/dev/cu.usbserial-*` to one process at a time. When monitor is running, flash can't open the port and vice versa.

If you run `make monitor` **inside a tmux pane**, the Makefile drops a breadcrumb at `.run/monitor-<port>.state` recording which tmux pane is holding which port. Subsequent `make flash` invocations (from any shell, including from inside the monitor pane itself) automatically:

1. read the breadcrumb,
2. send `Ctrl-C` to the monitor's tmux pane to release the port,
3. run the upload,
4. send a fresh `make monitor` invocation back into the same pane to resume.

Net effect: the tmux pane *is* the serial session for that port — scrollback preserved, monitor restarts in place. You can run flash commands from anywhere, including from inside that same monitor pane, and the upload/monitor swap happens automatically.

If you run `make monitor` **outside tmux**, no breadcrumb is recorded and there's no coordination — flash will fail with "port busy" until you Ctrl-C the monitor yourself. So: tmux is recommended for the serial session.

> **Emacs users**: nothing here is tmux-specific in a way that excludes you. If you'd rather host the serial session inside Emacs, run tmux inside an Emacs `vterm` (or `eat`) buffer and `make monitor` inside that. The coordination identifies panes by tmux's `pane_id`, not by terminal type, so it works the same regardless of whether the tmux pane is rendered by iTerm, Terminal.app, or Emacs vterm.

### Setting up the tmux session

The session name is entirely up to you — the coord scripts identify panes by tmux's `pane_id` (the stable `@N` / `%N` form), not by the session/window/pane name. Pick something memorable; the session name only shows up in log messages and `make monitors` output.

**Single board** — name the session after the project or the role:

```sh
tmux new -s ebus           # creates and attaches; sets up your serial session
make monitor               # auto-detects the single attached USB-serial port
```

**Multiple boards** — name each session after the specific device, so you can tell them apart at a glance:

```sh
# Terminal 1
tmux new -s ebus-poe
make monitor PIO_ENV=esp32-poe-iso PORT=/dev/cu.usbserial-21130

# Terminal 2
tmux new -s ebus-s3
make monitor PIO_ENV=esp32-s3-devkitc-1 PORT=/dev/cu.usbmodem14201
```

Run `make flash` (or `flash-all` / `uploadfs`) from any other shell — pass the same `PORT=` so the coord scripts target the right monitor:

```sh
make flash PORT=/dev/cu.usbserial-21130 PIO_ENV=esp32-poe-iso
```

### Multi-board support

Breadcrumbs are keyed by port (`monitor-cu.usbserial-21130.state`, `monitor-cu.usbmodem14201.state`, …) so monitoring multiple boards in parallel — each in its own tmux session — works fine:

- `make flash PORT=<port>` only pauses/restarts the monitor on that specific port; other sessions are untouched.
- `make flash` (no `PORT=`) with exactly one monitor running: acts on that one.
- `make flash` (no `PORT=`) with multiple monitors running: errors with a per-session listing of port/env/pane and asks you to specify `PORT=`.

Stale breadcrumbs (left behind by a force-killed tmux session) are detected via `tmux list-panes` and cleaned up the next time any coord script runs.

### `make monitors` — what's currently active

```
$ make monitors
1) /dev/cu.usbserial-21130
   env:     esp32-poe-iso
   pane:    ebus-poe:make.0 [%7]
   started: 2026-06-01T14:13:53Z

2) /dev/cu.usbmodem14201
   env:     esp32-s3-devkitc-1
   pane:    ebus-s3:zsh.0 [%12]
   started: 2026-06-01T14:18:42Z
```

If no sessions are running it prints `no active monitor sessions`. Pure cleanup helper — calling it also reaps any stale breadcrumbs as a side effect.

### Inspecting state (raw)

```sh
ls -la .run/                                   # raw breadcrumb files
cat .run/monitor-cu.usbserial-21130.state      # human-readable; cleanly removable if needed
```

`.run/` is gitignored.

## Config overlays — see Configuration.md

Two overlay files keep per-developer settings (MQTT credentials, upload-speed tweaks, etc.) out of the shared repo: `~/.config/eBus/config.yml` for firmware runtime config and `platformio_local.ini` for PlatformIO build/upload tweaks. The full model — flows, bootstrap commands, schemas, common overrides — lives in [`Configuration.md`](Configuration.md). Read that doc once after `make config-init` to understand both overlays end-to-end.

The CLI workflow above (`make fs`, `make flash-all`, etc.) all run through the overlay layer automatically. You don't need to think about it during ordinary development — only during initial setup or when something drifts.

For the long-term plan to move credentials out of LittleFS entirely and into NVS (runtime-provisioned via captive portal), see [`proposal-runtime-config-via-nvs.md`](proposal-runtime-config-via-nvs.md) — that's a design note awaiting discussion with Doug.

## Emacs integration

`pio` output is plain GCC-formatted compiler diagnostics (`file:line: warning: …`), which Emacs `compile-mode` parses out of the box. Workflow:

- **Build with error navigation**: `M-x compile RET` (the project's [`.dir-locals.el`](../.dir-locals.el) pre-fills `make build`) → `next-error` (`C-x ` `` ` ``) jumps to each warning/error in the source.
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
- [`PlatformIO_commands.md`](PlatformIO_commands.md) — raw `pio` command cheatsheet; useful when you want to invoke `pio` directly without `make`
- [`proposal-runtime-config-via-nvs.md`](proposal-runtime-config-via-nvs.md) — design note for the future NVS-backed runtime credential model
