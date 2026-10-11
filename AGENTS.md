# esp32-firmware

Instructions for LLM coding assistants working in this repository. Personal additions go in a gitignored local file (`CLAUDE.local.md` for Claude Code); see the README's "Using LLMs" section.

## What this is

Arduino-framework firmware, built with PlatformIO, that makes ESP32 / ESP32-S3 / Olimex ESP32-POE-ISO boards act as eBus devices or controllers over the [Homie 5 MQTT convention](https://homieiot.github.io/). A device is a project outside this repo, described by its `device.yml`; `./ebus-esp32 generate <project>` turns it into the Homie node and property tree in the project's `.ebus/`. The portable core (Homie model, `/set` dispatch, controller, links, discovery) is the [cpp-sdk](https://github.com/electrification-bus/cpp-sdk) library, a git submodule at `lib/ebus_core/`.

## Key files

- A project (user-owned, outside this repo): `device.yml` (device, mqtt, buses, nodes), `config-overlay.yml` merged over it at build time with the developer's secrets and local settings (never committed), optional hand-written code in `src/` and extra filesystem files in `data/`. The build writes everything into the project's `.ebus/`: `src/homie.cpp`, `data/config.yml`, `data/homie.yml`, `platformio.ini`, `build/<env>/`. Never edit `.ebus/` by hand.
- `examples/<name>/`: sample projects; `examples/demo` is the usual one to build and flash for testing. A build in place writes `examples/<name>/.ebus/` (gitignored).
- `ebus-esp32`: the CLI (generate, build, push, monitor, test). Python 3 with PyYAML. Commands take the project as their last argument.
- `platformio/platformio.ini`: the tracked environments (Arduino-ESP32 3.x unsuffixed, `-arduino2` for 2.0.17).
- `src/main.cpp`, `src/node/`, `src/platform/`: the firmware; a build links them into the project's `.ebus/src/`.
- `lib/<bus>/<component>.{h,cpp,yml}`: drivers, one per component; `lib/platform/modbus/` is the shared Modbus queue. New drivers are contributed here.
- `lib/ebus_core/`: the cpp-sdk submodule, pinned to a release tag. Core changes are PRs in cpp-sdk, followed by a submodule bump here.
- `doc/`: user and developer guides; `doc/WritingADriver.md` is the reference for drivers.

## Python environment

The CLI runs with whichever `python3` is first on `PATH`, so activate a virtual environment before running it. The README's Quick start creates `.venv/` at the repo root (gitignored):

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install pyyaml                       # the CLI
pip install -r tests/requirements.txt    # the CLI's unit tests (pytest, jsonschema)
pip install -r test/requirements.txt     # test/device_test.py against a live device (paho-mqtt)
```

A developer whose environment lives elsewhere names it in their local file, for example "activate `~/venvs/ebus` before running Python".

## Commands

```sh
./ebus-esp32 build examples/demo                          # generate + firmware + filesystem image
./ebus-esp32 push --port <serial-port> examples/demo      # flash over USB
./ebus-esp32 push --ota --host <device-ip> examples/demo  # flash over the network
./ebus-esp32 monitor --net --host <ip>                    # TCP log console (port 23)
./ebus-esp32 test                                         # Unity suites on the host: test/native/ and the core's
python -m pytest -q                                       # the CLI's unit tests (tests/cli/)
```

Build or flash only when the developer asks. Behavior changes are verified on a real board and broker, not just a green build.

Devices publish under `ebus/5/` (built with `-DUSE_EBUS_TOPIC`), not `homie/5/`; watch them with `mosquitto_sub -t 'ebus/5/#' -v`.

## Working with a real board

`./ebus-esp32` drives real hardware, and a behavior change is verified on a board. Ask before flashing: a board on the bench may be in use.

- Find the board: `./ebus-esp32 devices` lists serial ports. A `./ebus-esp32 monitor --serial` session learns the board's Device ID and IP (cached in `build/monitor-hosts.json`), after which `push --ota` and `monitor` reach it over the network. `--host` takes an IP or `<device-id>.local`.
- Flash over the network: `./ebus-esp32 build <project>`, then `./ebus-esp32 push --ota --host <ip> <project>`. It authenticates with `device.device_passphrase` from the project's `config-overlay.yml`. To test a change on a different board or device, build a scratch copy of an example (`cp -R examples/demo <scratch>/demo`) rather than editing the tracked one.
- Confirm the board came back: it reboots after a flash; `curl http://<ip>/api/ota` answers with a small `uptime_s` once it is up, and the device's `ebus/5/<device-id>/$state` returns to `ready`. Allow for DHCP and the broker connection, which can take tens of seconds. A successful push alone does not show the new firmware runs.
- Read the log over the network: `./ebus-esp32 monitor --net --host <ip>`, or `nc <ip> 23`. Opening the USB serial port can reset the board. A USB monitor and a USB flash share the port: the monitor pauses while the flash runs (see `doc/GettingStartedCLI.md`).
- Check behavior on MQTT: `mosquitto_sub -t 'ebus/5/<device-id>/#' -v` for published values, and `mosquitto_pub -t 'ebus/5/<device-id>/<node>/<property>/set' -m <value>` to exercise a settable property.
- Leave things as you found them: remove retained topics a test created (`mosquitto_pub -r -n -t <topic>`), and put the board back on the firmware it was running.

## Coding standards

The firmware and drivers run on microcontrollers:

- No dynamic allocation: objects are static, stack-allocated, or members of static objects. No `new`, `malloc`, or STL containers.
- No Arduino `String`; use `const char*`, fixed buffers and `snprintf`.
- No STL, `std::function`, lambdas or smart pointers; callbacks are C function pointers (with a `void*` context where needed).
- Drivers follow the Arduino `setup()` / `loop()` pattern. A constructor initializes only primitive members; property setup and hardware init happen in `setup()`, which calls the base class first.
- Background work runs in its own FreeRTOS task (the Modbus worker in `lib/platform/modbus/modbus_queue.cpp`) and exchanges data with the Arduino loop through FreeRTOS queues.
- Errors are return codes, not exceptions.
- Naming: classes `PascalCase`; methods match the surrounding code (`camelCase` or `snake_case`); members `_underscore_prefixed`; macros and constants `SCREAMING_SNAKE_CASE`; locals `snake_case`.
- Spec conformance wins: where the code and the Homie 5 convention or the [eBus specification](https://github.com/electrification-bus/specification) disagree, fix the code.

Markdown prose is written one paragraph per line, without hard wraps.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md): discuss substantive changes in an issue or discussion first, run the CLI tests when you change `ebus-esp32` or a component `.yml`, and reference rather than copy code from copyleft projects.
