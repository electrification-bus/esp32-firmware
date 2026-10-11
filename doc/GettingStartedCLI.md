# Getting Started — CLI / terminal workflow

This is the command-line counterpart to [`GettingStartedVSCode.md`](GettingStartedVSCode.md). If you're using VSCode + the PlatformIO extension, follow that doc instead — this one assumes you want to build, flash, and monitor from a terminal.

## What's in this workflow

- A small **`./bin/pio` shim** that points at PlatformIO Core installed under `~/.platformio/penv/`, or else at the first `pio` on `$PATH`. If the VSCode PlatformIO extension is installed, the shim already works.
- **`ebus-esp32`** — the CLI (a Python script with a shebang at the repo root; run it directly on Unix/Mac, or `ebus-esp32.bat` / `python <repo>/ebus-esp32` on Windows). All generate, build, flash, and monitor commands go through it. With the repo on your `PATH`, run it from your project directory as `ebus-esp32`.
- **A project**: a directory of your own, outside this repo, that this repo turns into firmware. It holds:
  - **`device.yml`** — the declarative device definition and the source of truth. `ebus-esp32 generate` (run automatically before every build by a PlatformIO pre-build hook) turns it into the project's `.ebus/src/homie.cpp`, `.ebus/data/homie.yml` and `.ebus/data/config.yml`. `.ebus/` also holds the build output and carries its own `.gitignore`.
  - A **per-developer config overlay**, `config-overlay.yml`, kept out of version control. There is no template to copy. You need one before the first build, because `generate` refuses an empty or `changeme` `device.device_passphrase`. It is written in `device.yml`'s own shape (the same `device:`/`wifi:`/`mqtt:`/`log:`/`mdns:` sections and key names), and its non-empty fields are merged into the generated `config.yml` during `generate`, so your real broker and credentials never land in `device.yml`.
  - Optionally **`src/`**, hand-written code compiled into the firmware ([HandCodedDevice.md](HandCodedDevice.md)), and **`data/`**, extra files for the filesystem image.

## Prerequisites

- Python 3.9+ with PyYAML. Install it in a virtual environment, since Homebrew Python and most Linux distributions refuse `pip install` into the system interpreter (PEP 668). From the repo root:

  ```sh
  python3 -m venv .venv
  . .venv/bin/activate        # Windows: .venv\Scripts\activate
  pip install pyyaml
  ```

  Activate the venv in each new shell before running `ebus-esp32`. `.venv/` is gitignored.
- **PlatformIO Core**. The CLI looks first at `~/.platformio/penv/bin/pio` (Unix/Mac) or `~/.platformio/penv/Scripts/pio.exe` (Windows), the paths the VSCode PlatformIO extension uses, then for `pio` on `$PATH`. If you don't have it, install via: VSCode + PlatformIO IDE extension, `pipx install platformio`, or `brew install platformio`.
- An **MQTT broker** the board can reach; set its address as `mqtt.host` in the project's `config-overlay.yml` (below). The README's [Quick start](../README.md#quick-start) has a note on Mosquitto 2.x.
- An ESP32 board with a USB-to-serial connection.

## First-time bootstrap (after a fresh clone)

```sh
git clone --recursive https://github.com/electrification-bus/esp32-firmware.git
cd esp32-firmware
export PATH="$PWD:$PATH"     # or add the repo to PATH in your shell profile

# Start a project from the demo, outside the repo.
cp -R examples/demo ~/my-device
cd ~/my-device

# Create a personal config overlay with only the keys you want to override.
# It uses device.yml's own sections and key names, so copy the block you care
# about out of device.yml and change the values. device_passphrase is required:
# generate refuses an empty or "changeme" value.
#
#   device:
#     device_passphrase: a-passphrase-of-your-own
#   wifi:
#     ssid: my-network
#     password: hunter2
#   mqtt:
#     host: broker.example.com
#
# There is no template to copy.
$EDITOR config-overlay.yml

# Sanity-check the toolchain and hardware
ebus-esp32 envs        # list board environments from platformio/platformio.ini
ebus-esp32 devices     # list attached USB-serial devices
```

On Windows, put the repo on `PATH` the same way and run `ebus-esp32.bat envs` (or `python <repo>\ebus-esp32 envs`).

Every command works on the project in the current directory, or on one named as its last argument (a directory, or its `device.yml`): `ebus-esp32 build ~/my-device`.

The overlay is **merged** into the generated `.ebus/data/config.yml` every time you `generate` (and thus before every build) — there is no separate "stage" step. Non-empty overlay fields win; everything else falls back to the values derived from `device.yml`.

## The build / flash / monitor loop

```sh
ebus-esp32 build            compile firmware + filesystem image (regenerates from device.yml first)
ebus-esp32 run              build and flash firmware + filesystem over USB
ebus-esp32 push             flash firmware + filesystem without forcing a rebuild
ebus-esp32 push --fs-only   flash filesystem only (re-flash config without rebuilding)
ebus-esp32 monitor          watch the device — network if its address is known, else USB (see below)
ebus-esp32 monitor --serial force the USB monitor — must be in a REAL terminal (see below)
ebus-esp32 run --monitor    flash, then attach the network console
ebus-esp32 serve            serve the built images for a device-initiated (pull) OTA
ebus-esp32 clean            clean build artifacts
```

Two ways to flash without a cable: `push --ota` sends the images to the device, and `push --pull --url <base>` tells the device to download them from a server instead — which is what works when the device can reach a server but you cannot reach the device. `serve` publishes the current build for that, and for driving a pull by hand from the device's own portal. See [ESP32OTA.md](ESP32OTA.md).

Edit the project's `device.yml` to change which board, nodes, or settings the firmware uses — every build path regenerates from it automatically. See [GenerateHomieDevice.md](GenerateHomieDevice.md) for the `device.yml` format and the component catalog.

Override the serial port when auto-detection picks the wrong one:

```sh
ebus-esp32 run --port COM3
ebus-esp32 monitor --port COM3
```

`ebus-esp32 envs` lists every `[env:*]` defined in `platformio/platformio.ini`.

## Network console — no USB cable

The firmware tees its console to a TCP listener (`include/platform/log_stream.h`), so an installed board with no reachable USB port can still be watched. `pio device monitor` speaks pyserial `socket://` URLs natively, so this needs no extra tooling — and the device replays its ring buffer to each new client, so you still get the boot log after connecting late.

```sh
ebus-esp32 monitor                        # network if an address is known, else USB
ebus-esp32 monitor --net --host 10.0.0.5  # a specific device
ebus-esp32 run --monitor                  # flash, wait for the board to rejoin, then attach
ebus-esp32 build --monitor                # attach to a board that is already running
```

**How the address is found**, in order:

1. `--host <ip|name|Device ID>`
2. the address learned from a previous USB session, cached in the repo's `build/monitor-hosts.json` (gitignored) per board, under the Device ID the firmware printed, along with the `device.yml` path and `device.name` it was seen from. When more than one board has been learned from the same `device.yml`, the CLI lists them; `push --ota` and `--pull` then stop and ask for `--host`, which takes an IP or one of the listed Device IDs, and `monitor` attaches to the most recently seen one and says so. An address from a session whose boot log lacked the Device ID line is listed among them as the newest, unless a known board from that `device.yml` was last seen at it. `push --ota` and `--pull` print which device and address they chose

With neither, pass `--host <device-id>.local`: arduino-esp32 3.x builds always claim the device ID printed at boot as their mDNS hostname (see [MDNS.md](MDNS.md#hostname)).

The cache is what makes the bare `--net` form work. `ebus-esp32 monitor --serial` populates it by watching for the `Network: WiFi STA IP=` / `Network: Ethernet IP=` lines the firmware prints at boot: **flash over USB once, and every later network monitor knows where the board is.** To do that the CLI relays the serial stream through itself, which costs interactivity (keystrokes are no longer forwarded to the device) — pass `--no-learn` to keep pio attached directly to the terminal.

The port comes from `log.tcp_port` in `device.yml` (default 23; `0` disables the listener):

```yaml
log:
  tcp_port: 23
```

Two caveats. The stream is **unauthenticated** — anyone on the LAN can read it, so set `tcp_port: 0` on untrusted networks. And **crashes do not arrive over it**: panic handlers and backtraces are written below this layer, after the scheduler is gone, so a board's last words still need the USB cable.

## Serial monitor — needs a real terminal

`ebus-esp32 monitor` runs `pio device monitor`, which is interactive (it puts the controlling terminal into raw mode to capture single keystrokes). It **requires a real PTY**: a true terminal window like Terminal.app / iTerm / the VSCode integrated terminal / a tmux pane / Windows Terminal.

It will *not* work if launched from:
- A non-interactive `bash -c '...'` invocation
- Output-capturing harnesses (CI pipelines, scripted automation, LLM coding-assistant shells)

If `monitor` errors out with `termios.error: Operation not supported on socket` (errno 102) or `Operation not supported by device` (errno 19), that's the symptom — open a real terminal window and run it there instead. Useful keys once inside miniterm: `Ctrl-T h` for the menu, `Ctrl-T q` to quit cleanly (releases the port).

For *read-only* serial capture that works anywhere — e.g. for piping into a log file — use raw `cat` after `stty` setup:

```sh
stty -f /dev/cu.usbserial-XXXX 115200 cs8 -cstopb -parenb raw -echo
cat /dev/cu.usbserial-XXXX | tee /tmp/serial.log
```

## Monitor ↔ flash port handoff

A serial monitor and a flash both need exclusive access to the same USB-serial port. `ebus-esp32` hands the port over automatically: leave a USB monitor running in one terminal, flash from another, and the monitor prints `--- monitor paused: a flash is using <port> ---`, the flash runs, and the monitor prints `--- monitor resumed on <port> ---` and carries on with the same port and options. This works in any terminal (VSCode's integrated terminal, Terminal.app, iTerm, tmux, Linux terminals, Windows Terminal).

```sh
# Terminal A
ebus-esp32 monitor --serial                         # pins the first USB serial port it finds
# Terminal B
ebus-esp32 run                                      # or: push, push --fs-only, run --erase
```

Multiple boards: pin each monitor to its port, and name the port when flashing.

```sh
ebus-esp32 monitor --port /dev/cu.usbserial-XXXX    # terminal A
ebus-esp32 monitor --port /dev/cu.usbmodem14201     # terminal B
ebus-esp32 run --port /dev/cu.usbserial-XXXX        # terminal C: pauses only A
```

`ebus-esp32 monitors` lists the running monitors, their ports, and whether each is watching or paused.

How it works: the USB monitor runs `pio device monitor` as a child process and writes `.run/monitor-<port>.state` with its PID. A flash writes `.run/flash-<port>.request`; the monitor, which checks for it every 0.2 s, stops its child and writes `.run/monitor-<port>.released`. The flash waits for that file, uploads, and removes its request, at which point the monitor waits for the port to reappear (a native-USB board re-enumerates after reset) and starts the child again. `/dev/cu.X` and `/dev/tty.X` count as the same port.

- **No monitor running:** the flash proceeds immediately.
- **Monitor crashed or its terminal closed:** its breadcrumb's PID is dead, so the flash removes it and proceeds. A request left by a flash that crashed is likewise ignored and the monitor resumes.
- **Several monitors and no `--port` on the flash:** the flash stops with an error listing the ports.
- **Monitor does not let go within 20 s:** the flash stops with an error naming the monitor's PID. Set `EBUS_HANDOFF_TIMEOUT=<seconds>` to wait longer.
- **Network monitor (`--net`):** holds no serial port, so a flash leaves it alone.

## Config overlays — see Configuration.md

Two overlay files keep per-developer settings out of version control: the project's `config-overlay.yml` for firmware runtime config and this repo's `platformio_local.ini` for PlatformIO build/upload tweaks. The full model lives in [`Configuration.md`](Configuration.md). Read that doc once, after creating an overlay, to understand both overlays end-to-end.

## Emacs integration

`pio` output is plain GCC-formatted compiler diagnostics (`file:line: warning: …`), which Emacs `compile-mode` parses out of the box. Workflow:

- **Build with error navigation**: `M-x compile RET ebus-esp32 build RET` → `next-error` (`C-x `` ` ``) jumps to each warning/error in the source. The project's [`.dir-locals.el`](../.dir-locals.el) pre-fills the compile command.
- **Quick rebuild**: `M-x recompile` re-runs the last compile command without re-prompting.
- **Serial monitor**: do *not* use `M-x serial-term` for this — open a separate terminal and run `ebus-esp32 monitor` there.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `ERROR: PyYAML is not installed for ...` | PyYAML not installed for the `python3` that ran the CLI | Activate the repo venv (`. .venv/bin/activate`), or create it as in [Prerequisites](#prerequisites) |
| PlatformIO not found | `~/.platformio/penv/...pio` doesn't exist and no `pio` is on `$PATH` | Install via VSCode PlatformIO extension, `pipx install platformio`, or `brew install platformio` |
| `run` / `push` finds no port / hangs at "Connecting…" | No board attached, wrong port, or board not in bootloader state | `ebus-esp32 devices` to list ports; some boards need BOOT held during reset for first flash |
| `monitor` → `termios.error` | Running through a non-PTY context | Run in a real terminal window |
| MQTT/wifi settings from your overlay aren't taking effect | Overlay missing, or a field left empty (empty fields don't override) | Confirm `config-overlay.yml` exists and the field is non-empty, then re-run `ebus-esp32 generate` — it prints `overlay: <path> (N field(s))` and errors out on any key it doesn't recognize. `ebus-esp32 config-status` shows whether an overlay is active |
| `ERROR: this repo is the firmware, not a project` | A command ran in the repo with no project named | Name the project (`ebus-esp32 build ~/my-device`), or run from its directory |
| Device boots **stale firmware** after a flash that "verified" | The board's partition table is dual-OTA (two app slots); a confused `otadata` can boot the *previous* slot. Most often caused by a *combined* `pio run -t upload -t buildfs -t uploadfs` | Use `ebus-esp32 run`/`push` (they flash firmware and filesystem in **separate** invocations, which avoids this). To recover a wedged board: `ebus-esp32 run --erase` (full chip erase first). Never combine `-t upload` with `-t buildfs/uploadfs` in one `pio run`. |
| Suspect a stale build object | `pio run -t clean` is unreliable here | `ebus-esp32 clean` (removes the env build dir outright), then rebuild |

## Reference

- [`ebus-esp32`](../ebus-esp32) — CLI entry point; `ebus-esp32 --help` lists all commands
- [`GenerateHomieDevice.md`](GenerateHomieDevice.md) — the `device.yml` format and the library-component catalog
- [`bin/pio`](../bin/pio) — PlatformIO Core shim
- [`PlatformIOCommands.md`](PlatformIOCommands.md) — raw `pio` command cheatsheet for when you need to invoke `pio` directly
- [`proposals/RuntimeConfigViaNVS.md`](proposals/RuntimeConfigViaNVS.md) — design note for the future NVS-backed runtime credential model
