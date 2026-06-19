# Getting Started — CLI / terminal workflow

This is the command-line counterpart to [`GettingStarted_VSCode.md`](GettingStarted_VSCode.md). If you're using VSCode + the PlatformIO extension, follow that doc instead — this one assumes you want to build, flash, and monitor from a terminal.

## What's in this workflow

- A small **`./bin/pio` shim** that points at PlatformIO Core installed under `~/.platformio/penv/`. No `$PATH` edits required — if the VSCode PlatformIO extension is installed, the shim already works.
- **`homie.py`** — the project CLI. All build, flash, monitor, and config commands go through it.
- A **per-developer config overlay** at `~/.config/eBus/config.yml` (gitignored, never committed). The repo carries `data/config.yml.example` as the schema + safe-defaults template; the staged build file `data/config.yml` is generated from the overlay just-in-time.

## Prerequisites

- Python 3.9+ and `pip install pyyaml`.
- **PlatformIO Core** installed somewhere `./bin/pio` can find. The default is `~/.platformio/penv/bin/pio` (Unix/Mac) or `~/.platformio/penv/Scripts/pio.exe` (Windows) — the paths the VSCode PlatformIO extension uses. If you don't have it, install via: VSCode + PlatformIO IDE extension, `pipx install platformio`, or `brew install platformio`.
- An ESP32 board with a USB-to-serial connection.

## First-time bootstrap (after a fresh clone)

```cmd
git clone <repo-url>
cd esp32-sdk

# Bootstrap your personal config overlay from the schema example.
# Creates ~/.config/eBus/config.yml if missing; no-op if present.
python homie.py config init

# Edit it with your MQTT broker, credentials, device id, etc.
# (The defaults point at the public Shiftr.io test broker for first-boot
# smoke testing — replace with your real broker for any actual use.)
edit %USERPROFILE%\.config\eBus\config.yml

# Sanity-check
python homie.py config status
python homie.py envs
python homie.py devices
```

`config init` is idempotent — running it a second time will tell you the overlay already exists and refuse to clobber. `config status` shows the overlay path, whether it's present, and whether `data/config.yml` is in sync.

## The build / flash / monitor loop

```cmd
python homie.py build            compile firmware (runs generate first)
python homie.py run              generate, build, and flash firmware + filesystem over USB
python homie.py push             flash pre-built firmware + filesystem (no build)
python homie.py push --fs-only   flash filesystem only (re-flash config without rebuilding)
python homie.py monitor          open serial monitor — must be in a REAL terminal (see below)
python homie.py clean            clean build artifacts
```

Override the serial port when auto-detection picks the wrong one:

```cmd
python homie.py run --port COM3
python homie.py monitor --port COM3
```

`python homie.py envs` lists every `[env:*]` defined in `platformio.ini`.

## Serial monitor — needs a real terminal

`python homie.py monitor` runs `pio device monitor`, which is interactive (it puts the controlling terminal into raw mode to capture single keystrokes). It **requires a real PTY**: a true terminal window like Terminal.app / iTerm / a tmux pane / Windows Terminal.

It will *not* work if launched from:
- A non-interactive `bash -c '...'` invocation
- Output-capturing harnesses (CI pipelines, scripted automation)
- Claude Code's shell-prefix in this session

If `monitor` errors out with `termios.error: Operation not supported on socket` (errno 102) or `Operation not supported by device` (errno 19), that's the symptom — open a real terminal window and run it there instead. Useful keys once inside miniterm: `Ctrl-T h` for the menu, `Ctrl-T q` to quit cleanly (releases the port).

For *read-only* serial capture that works anywhere — e.g. for piping into a log file — use raw `cat` after `stty` setup:

```sh
stty -f /dev/cu.usbserial-21130 115200 cs8 -cstopb -parenb raw -echo
cat /dev/cu.usbserial-21130 | tee /tmp/serial.log
```

## Monitor ↔ flash port handoff (when monitor runs in tmux)

`python homie.py monitor` and `python homie.py run` / `python homie.py push` both need exclusive access to the serial port. When monitor is running, run/push can't open the port and vice versa.

If you run monitor **inside a tmux pane**, the `bin/_monitor-record` / `bin/_flash-coord` shell scripts (invoked by the underlying `pio` call) drop a breadcrumb at `.run/monitor-<port>.state` recording which tmux pane holds which port. Subsequent run/push invocations automatically:

1. read the breadcrumb,
2. send `Ctrl-C` to the monitor's tmux pane to release the port,
3. run the upload,
4. send a fresh monitor invocation back into the same pane to resume.

If you run monitor **outside tmux**, no breadcrumb is recorded — run/push will fail with "port busy" until you Ctrl-C the monitor yourself.

### Setting up the tmux session

```sh
tmux new -s ebus
python homie.py monitor          # auto-detects the single attached USB-serial port
```

Multiple boards — one tmux session per device:

```sh
# Terminal 1
tmux new -s ebus-poe
python homie.py monitor --port /dev/cu.usbserial-21130

# Terminal 2
tmux new -s ebus-s3
python homie.py monitor --port /dev/cu.usbmodem14201
```

Push from any other shell, passing the same port:

```sh
python homie.py run --port /dev/cu.usbserial-21130
```

## Config overlays — see Configuration.md

Two overlay files keep per-developer settings out of the shared repo: `~/.config/eBus/config.yml` for firmware runtime config and `platformio_local.ini` for PlatformIO build/upload tweaks. The full model lives in [`Configuration.md`](Configuration.md). Read that doc once after `python homie.py config init` to understand both overlays end-to-end.

## Emacs integration

`pio` output is plain GCC-formatted compiler diagnostics (`file:line: warning: …`), which Emacs `compile-mode` parses out of the box. Workflow:

- **Build with error navigation**: `M-x compile RET python homie.py build RET` → `next-error` (`C-x `` ` ``) jumps to each warning/error in the source. The project's [`.dir-locals.el`](../.dir-locals.el) pre-fills the compile command.
- **Quick rebuild**: `M-x recompile` re-runs the last compile command without re-prompting.
- **Serial monitor**: do *not* use `M-x serial-term` for this — open a separate terminal (or tmux pane) and run `python homie.py monitor` there.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `ModuleNotFoundError: No module named 'yaml'` | PyYAML not installed | `pip install pyyaml` |
| PlatformIO not found | `~/.platformio/penv/...pio` doesn't exist | Install via VSCode PlatformIO extension or `pipx install platformio` |
| `run` / `push` finds no port / hangs at "Connecting…" | No board attached, wrong port, or board not in bootloader state | `python homie.py devices` to list ports; some boards need BOOT held during reset for first flash |
| `monitor` → `termios.error` | Running through a non-PTY context | Run in a real terminal window |
| `config status` shows overlay missing | First-run state | `python homie.py config init`, then edit the overlay |
| `data/config.yml` showed up modified after pulling | Someone else's overlay snuck into a commit (shouldn't happen — it's gitignored) | `git checkout data/config.yml`, then `python homie.py config stage` to re-stage from overlay |

## Reference

- [`homie.py`](../homie.py) — CLI entry point; `python homie.py --help` lists all commands
- [`bin/pio`](../bin/pio) — PlatformIO Core shim
- [`PlatformIO_commands.md`](PlatformIO_commands.md) — raw `pio` command cheatsheet for when you need to invoke `pio` directly
- [`proposal-runtime-config-via-nvs.md`](proposal-runtime-config-via-nvs.md) — design note for the future NVS-backed runtime credential model
