# Getting Started — CLI / terminal workflow

This is the command-line counterpart to [`GettingStartedVSCode.md`](GettingStartedVSCode.md). If you're using VSCode + the PlatformIO extension, follow that doc instead — this one assumes you want to build, flash, and monitor from a terminal.

## What's in this workflow

- A small **`./bin/pio` shim** that points at PlatformIO Core installed under `~/.platformio/penv/`. No `$PATH` edits required — if the VSCode PlatformIO extension is installed, the shim already works.
- **`./ebus-esp32`** — the project CLI (a Python script with a shebang; run it directly on Unix/Mac, or `ebus-esp32.bat` / `python ebus-esp32` on Windows). All generate, build, flash, and monitor commands go through it.
- **`device.yml`** — the declarative device definition and the source of truth. `./ebus-esp32 generate` (run automatically before every build by a PlatformIO pre-build hook) turns it into `src/homie.cpp`, `data/homie.yml`, and `data/config.yml`. Those three are **gitignored** — regenerated, never committed.
- A **per-developer config overlay** at `config-overlay.yml` (gitignored, never committed). There is no template to copy — create it only if you need it. It is written in `device.yml`'s own shape (the same `device:`/`wifi:`/`mqtt:`/`log:`/`mdns:` sections and key names), and its non-empty fields are merged into the generated `data/config.yml` during `generate`, so your real broker and credentials never land in `device.yml`.

## Prerequisites

- Python 3.9+ and `pip install pyyaml`.
- **PlatformIO Core** installed somewhere `./bin/pio` can find. The default is `~/.platformio/penv/bin/pio` (Unix/Mac) or `~/.platformio/penv/Scripts/pio.exe` (Windows) — the paths the VSCode PlatformIO extension uses. If you don't have it, install via: VSCode + PlatformIO IDE extension, `pipx install platformio`, or `brew install platformio`.
- An ESP32 board with a USB-to-serial connection.

## First-time bootstrap (after a fresh clone)

```sh
git clone <repo-url>
cd esp32-sdk

# Optional: create a personal config overlay with only the keys you want to
# override. It uses device.yml's own sections and key names, so copy the block
# you care about out of device.yml and change the values:
#
#   wifi:
#     ssid: my-network
#     password: hunter2
#   mqtt:
#     host: broker.example.com
#
# There is no template to copy — create the file only if you need it.
$EDITOR config-overlay.yml

# Sanity-check the toolchain and hardware
./ebus-esp32 envs        # list board environments from platformio/platformio.ini
./ebus-esp32 devices     # list attached USB-serial devices
```

On Windows, run the CLI as `ebus-esp32.bat envs` (or `python ebus-esp32 envs`).

The overlay is **merged** into the generated `data/config.yml` every time you `generate` (and thus before every build) — there is no separate "stage" step. Non-empty overlay fields win; everything else falls back to the values derived from `device.yml`.

## The build / flash / monitor loop

```sh
./ebus-esp32 build            compile firmware + filesystem image (regenerates from device.yml first)
./ebus-esp32 run              build and flash firmware + filesystem over USB
./ebus-esp32 push             flash firmware + filesystem without forcing a rebuild
./ebus-esp32 push --fs-only   flash filesystem only (re-flash config without rebuilding)
./ebus-esp32 monitor          watch the device — network if its address is known, else USB (see below)
./ebus-esp32 monitor --serial force the USB monitor — must be in a REAL terminal (see below)
./ebus-esp32 run --monitor    flash, then attach the network console
./ebus-esp32 serve            serve the built images for a device-initiated (pull) OTA
./ebus-esp32 clean            clean build artifacts
```

Two ways to flash without a cable: `push --ota` sends the images to the device, and
`push --pull --url <base>` tells the device to download them from a server instead —
which is what works when the device can reach a server but you cannot reach the device.
`serve` publishes the current build for that, and for driving a pull by hand from the
device's own portal. See [ESP32OTA.md](ESP32OTA.md).

Edit `device.yml` to change which board, nodes, or settings the firmware uses — every build path regenerates from it automatically. See [GenerateHomieDevice.md](GenerateHomieDevice.md) for the `device.yml` format and the component catalog.

Override the serial port when auto-detection picks the wrong one:

```sh
./ebus-esp32 run --port COM3
./ebus-esp32 monitor --port COM3
```

`./ebus-esp32 envs` lists every `[env:*]` defined in `platformio/platformio.ini`.

## Network console — no USB cable

The firmware tees its console to a TCP listener (`include/platform/log_stream.h`), so an installed board with no reachable USB port can still be watched. `pio device monitor` speaks pyserial `socket://` URLs natively, so this needs no extra tooling — and the device replays its ring buffer to each new client, so you still get the boot log after connecting late.

```sh
./ebus-esp32 monitor                        # network if an address is known, else USB
./ebus-esp32 monitor --net --host 10.0.0.5  # a specific device
./ebus-esp32 run --monitor                  # flash, wait for the board to rejoin, then attach
./ebus-esp32 build --monitor                # attach to a board that is already running
```

**How the address is found**, in order:

1. `--host <ip|name>`
2. the address learned from a previous USB session, cached per board in `build/monitor-hosts.json` (gitignored)
3. `<device.name>.local` — needs the mDNS **responder** (`mdns.advertise`, or the legacy
   `mdns.enabled` which sets both gates). On by default for Ethernet-only builds on
   arduino-esp32 3.x, off otherwise; pin it with an `mdns:` section in `device.yml`
   (see [MDNS.md](MDNS.md))

The cache is what makes the bare `--net` form work. `./ebus-esp32 monitor --serial` populates it by watching for the `Network: WiFi STA IP=` / `Network: Ethernet IP=` lines the firmware prints at boot: **flash over USB once, and every later network monitor knows where the board is.** To do that the CLI relays the serial stream through itself, which costs interactivity (keystrokes are no longer forwarded to the device) — pass `--no-learn` to keep pio attached directly to the terminal.

The port comes from `log.tcp_port` in `device.yml` (default 23; `0` disables the listener):

```yaml
log:
  tcp_port: 23
```

Two caveats. The stream is **unauthenticated** — anyone on the LAN can read it, so set `tcp_port: 0` on untrusted networks. And **crashes do not arrive over it**: panic handlers and backtraces are written below this layer, after the scheduler is gone, so a board's last words still need the USB cable.

## Serial monitor — needs a real terminal

`./ebus-esp32 monitor` runs `pio device monitor`, which is interactive (it puts the controlling terminal into raw mode to capture single keystrokes). It **requires a real PTY**: a true terminal window like Terminal.app / iTerm / a tmux pane / Windows Terminal.

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

A serial monitor and a flash both need exclusive access to the same USB-serial port — while one holds it, the other can't open it. `./ebus-esp32` coordinates this automatically when the monitor runs **inside a tmux pane**: `./ebus-esp32 monitor` records which pane holds the port, and a later `./ebus-esp32 run` / `push` pauses it, flashes, and resumes it.

When you start a monitor in a tmux pane, `bin/_monitor-record` drops a breadcrumb at `.run/monitor-<port>.state` recording which tmux pane holds which port (and which front-end started it). A subsequent flash then, via `bin/_flash-coord`, automatically:

1. reads the breadcrumb,
2. sends `Ctrl-C` to the monitor's tmux pane to release the port,
3. runs the upload,
4. relaunches the *same* `./ebus-esp32 monitor` back into that pane to resume.

If you run the monitor **outside tmux** (or on Windows), no breadcrumb is recorded and there's nothing to coordinate — the flash will fail with "port busy" until you Ctrl-C the monitor yourself. `./ebus-esp32 monitors` lists the active monitor breadcrumbs.

### Setting up the tmux session

```sh
tmux new -s ebus
./ebus-esp32 monitor          # auto-detects the single attached USB-serial port
```

Multiple boards — one tmux session per device, pinning the port:

```sh
# Terminal 1
tmux new -s ebus-poe
./ebus-esp32 monitor --port /dev/cu.usbserial-21130

# Terminal 2
tmux new -s ebus-s3
./ebus-esp32 monitor --port /dev/cu.usbmodem14201
```

Flash from any other shell, passing the same port — the monitor pane pauses and resumes on its own:

```sh
./ebus-esp32 run --port /dev/cu.usbserial-21130
```

## Config overlays — see Configuration.md

Two overlay files keep per-developer settings out of the shared repo: `config-overlay.yml` for firmware runtime config and `platformio_local.ini` for PlatformIO build/upload tweaks. The full model lives in [`Configuration.md`](Configuration.md). Read that doc once, after creating an overlay, to understand both overlays end-to-end.

## Emacs integration

`pio` output is plain GCC-formatted compiler diagnostics (`file:line: warning: …`), which Emacs `compile-mode` parses out of the box. Workflow:

- **Build with error navigation**: `M-x compile RET ./ebus-esp32 build RET` → `next-error` (`C-x `` ` ``) jumps to each warning/error in the source. The project's [`.dir-locals.el`](../.dir-locals.el) pre-fills the compile command.
- **Quick rebuild**: `M-x recompile` re-runs the last compile command without re-prompting.
- **Serial monitor**: do *not* use `M-x serial-term` for this — open a separate terminal (or tmux pane) and run `./ebus-esp32 monitor` there.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `ERROR: PyYAML is not installed for ...` | PyYAML not installed for the `python3` that ran the CLI | `pip install pyyaml` |
| PlatformIO not found | `~/.platformio/penv/...pio` doesn't exist | Install via VSCode PlatformIO extension or `pipx install platformio` |
| `run` / `push` finds no port / hangs at "Connecting…" | No board attached, wrong port, or board not in bootloader state | `./ebus-esp32 devices` to list ports; some boards need BOOT held during reset for first flash |
| `monitor` → `termios.error` | Running through a non-PTY context | Run in a real terminal window |
| MQTT/wifi settings from your overlay aren't taking effect | Overlay missing, or a field left empty (empty fields don't override) | Confirm `config-overlay.yml` exists and the field is non-empty, then re-run `./ebus-esp32 generate` — it prints `overlay: <path> (N field(s))` and errors out on any key it doesn't recognise. `./ebus-esp32 config-status` shows whether an overlay is active |
| `src/homie.cpp` or `data/homie.yml` shows up as an untracked/modified file | These are generated outputs — gitignored, regenerated every build | Normal; never commit them. If one is *tracked* in your clone, `git rm --cached <file>` |
| Device boots **stale firmware** after a flash that "verified" | The board's partition table is dual-OTA (two app slots); a confused `otadata` can boot the *previous* slot. Most often caused by a *combined* `pio run -t upload -t buildfs -t uploadfs` | Use `./ebus-esp32 run`/`push` (they flash firmware and filesystem in **separate** invocations, which avoids this). To recover a wedged board: `./ebus-esp32 run --erase` (full chip erase first). Never combine `-t upload` with `-t buildfs/uploadfs` in one `pio run`. |
| Suspect a stale build object | `pio run -t clean` is unreliable here | `./ebus-esp32 clean` (removes the env build dir outright), then rebuild |

## Reference

- [`ebus-esp32`](../ebus-esp32) — CLI entry point; `./ebus-esp32 --help` lists all commands
- [`GenerateHomieDevice.md`](GenerateHomieDevice.md) — the `device.yml` format and the library-component catalog
- [`bin/pio`](../bin/pio) — PlatformIO Core shim
- [`PlatformIOCommands.md`](PlatformIOCommands.md) — raw `pio` command cheatsheet for when you need to invoke `pio` directly
- [`proposals/RuntimeConfigViaNVS.md`](proposals/RuntimeConfigViaNVS.md) — design note for the future NVS-backed runtime credential model
