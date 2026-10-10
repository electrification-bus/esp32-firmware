# Contributing to esp32-firmware

Thanks for your interest in contributing! `esp32-firmware` is firmware that lets ESP32-class hardware act as an eBus-compatible device (or controller) using the [Homie 5 MQTT convention](https://homieiot.github.io/). It targets PlatformIO + the Arduino framework on ESP32 / ESP32-S3 / Olimex ESP32-POE-ISO, with pluggable nodes for sensors, actuators, Modbus devices, and energy metering. A device is described declaratively in `device.yml`, and the build generates the Homie node/property tree from it.

## How to contribute

### Discussions

Use [Discussions](https://github.com/electrification-bus/esp32-firmware/discussions) for:

- Open-ended questions about the firmware's design, scope, or intent ("should the bridge/proxy model handle X, or does that belong upstream?")
- Proposed new node types, bus drivers, or hardware targets — worth aligning on the `device.yml` shape and the runtime API before writing the code
- Integration questions ("how do I model my device as a nested Homie 5 device?") that aren't yet a clear bug or feature request
- Thinking out loud about a proposed change before scoping it

Discussions are open-ended — a good place to align on direction before something becomes a concrete change. Aligned outcomes often turn into one or more Issues or pull requests.

### Issues

Use [Issues](https://github.com/electrification-bus/esp32-firmware/issues) for actionable changes:

- Bug reports with reproduction steps (board, build env, `device.yml`, broker, and ideally a serial-log snippet)
- Concrete feature requests with a clear scope and a use case
- Documentation gaps where a specific doc or comment change is intended
- Discussion outcomes that have alignment and a clear scope

If you're not sure whether something is an Issue or a Discussion, start with a Discussion — we can convert it later.

### Pull requests

Pull requests are welcome.

- For small fixes (typos, doc tweaks, low-risk bug fixes), open a PR directly.
- For substantive changes (new public runtime API, changes to the `device.yml` schema or the generator, new bus drivers or hardware targets, changes that alter Homie/eBus wire behavior), open a Discussion or Issue first so we can align on scope before you invest the effort.
- **Spec conformance is the north star.** The [Homie 5 convention](https://homieiot.github.io/) and the [Electrification Bus specification](https://github.com/electrification-bus/specification) are the source of truth for on-the-wire behavior — topic structure, datatypes, `$state` semantics, the nested/proxied device model, the eBus device- and capability-type vocabularies. When firmware behavior and the spec disagree, the spec wins; fix the firmware rather than codifying the deviation.
- **Update `.ebus-spec.json` on every spec sync.** When you reconcile the firmware with a newer specification commit, bump `synced_commit`, `synced_date`, and `framework`, and edit `supports` to match the framework features the code now implements (see the specification's `conventions/spec-provenance.md`).
- **`device.yml` is the source of truth.** The generated `src/homie.cpp`, `data/homie.yml`, and `data/config.yml` are gitignored build artifacts — don't edit or commit them. Change `device.yml` (and the `lib/<bus>/*.yml` component definitions) and let the generator produce the rest. The hand-coded path (see [doc/HandCodedDevice.md](doc/HandCodedDevice.md)) is for devices that bypass the generator entirely.
- **House style: no dynamic allocation, no Arduino `String`, no STL, no lambdas.** This is conservative embedded firmware. Use fixed-size buffers and static storage, `snprintf` over string concatenation, and the C function-pointer + `void*` context idiom for callbacks (see the `on_set` handlers in [doc/WritingADriver.md](doc/WritingADriver.md)). Members are underscore-prefixed. Stay inside the Arduino `setup()` / `loop()` model; long-running or multi-core work goes through FreeRTOS queues, not blocking calls. This is a real constraint, not a preference — code that allocates on the heap or pulls in the STL will be asked to change.
- **Test changes end-to-end on real hardware.** A green build is necessary but not sufficient. Flash the firmware to an actual ESP32, point it at a live MQTT broker, and confirm the Homie tree publishes and behaves as intended (an [MCP client](doc/Homie5MCPServer.md) makes inspecting the live device easy). Anything touching the wire or the hardware needs a bench check before it ships.
- **Run the CLI tests when you change `./ebus-esp32` or a `lib/<bus>/*.yml` component.** `pip install -r tests/requirements.txt`, then `python -m pytest -q` from the repo root. The suite in `tests/cli/` covers the generator's `device.yml` validation, the config overlay, the learned-address cache and the OTA outcome logic; it needs neither PlatformIO nor a board, and CI runs it on every push. `test/device_test.py` is separate: it checks a live device over MQTT (see [test/Test_Guide.md](test/Test_Guide.md)).
- **Change the portable core in [cpp-sdk](https://github.com/electrification-bus/cpp-sdk), then bump the submodule here.** `lib/ebus_core/` is a git submodule pinned to a cpp-sdk release tag, and CI checks that it is at one. Send core changes (the Homie device model, `/set` dispatch, the controller, ids, payloads, broker discovery) to cpp-sdk as a pull request; its CI builds the core with GCC and Clang and runs its tests and the POSIX port. While you work, you can edit and commit inside `lib/ebus_core/` and build the firmware against it. Once cpp-sdk tags a release, move the submodule and commit the new pointer: `git -C lib/ebus_core fetch --tags && git -C lib/ebus_core checkout <tag> && git add lib/ebus_core`.
- **Run the native unit tests when you change ESP32 code they cover or move the submodule.** `./ebus-esp32 test` builds the Unity suites in `test/native/` and the core's own suites in `lib/ebus_core/test/` for the host with PlatformIO's `native` platform and runs them; it needs PlatformIO and a host C++ compiler, but no board, and CI runs it on every push. To add a suite for ESP32-side code, create `test/native/test_<name>/`; it includes the source file it tests and supplies Arduino stand-ins in its own directory, as `test_log_stream` does. `[env:native]` (`platformio/platformio.ini`) links `lib/ebus_core`, so a suite can also call any core function.
- **One command surface: `./ebus-esp32`.** There is no Makefile — `./ebus-esp32 generate|build|run|push|monitor` is the front door (it wraps `pio` and preserves the serial-port sharing between a monitor and a flash). Use it (or raw `pio` for hand-coded sketches) so behavior stays consistent across contributors.
- **Respect upstream licenses — reference, don't copy.** This firmware is MIT. Do **not** paste source from copyleft projects into it. In particular, [ESPHome](https://esphome.io/)'s C++ is GPLv3 — it's an excellent *reference* for how to talk to a chip, but its code can't live here. Learn the protocol from it and reimplement in this repo's style; see [doc/WritingADriver.md](doc/WritingADriver.md#borrowing-protocol-knowledge-from-esphome-reference-dont-copy). PRs that copy GPL/copyleft code will be asked to reimplement.
- **Keep comments to a minimum.** The project style is to write self-explanatory code and reserve comments for non-obvious *why* (a hardware quirk, a spec corner, a workaround for a specific ESP-IDF bug). Don't add comments that just restate the code.
- **Don't hard-wrap Markdown prose.** Write each paragraph in docs as a single continuous line; reserve hard breaks for list items, headings, code fences, and tables.
- One commit per logical change is fine; we don't require squash or any particular branch naming.

## LLM coding assistants

[`AGENTS.md`](AGENTS.md) (imported by `CLAUDE.md`) holds the shared instructions for LLM coding assistants; change it by pull request when the project's conventions change. Keep your personal configuration (`CLAUDE.local.md`, `.claude/`, `.cursor/` and the like) out of the repo: those paths are gitignored, and the README's "Using LLMs" section shows the shadow-repo symlink pattern.

## Code of conduct

Be respectful and constructive. We appreciate everyone who takes the time to file an issue, start a discussion, or send a pull request.

## Maintenance posture

`esp32-firmware` is an active alpha project. Updates and maintenance, including responses to issues filed on GitHub, will take place on an "as time and resources permit" basis. The firmware is maintained alongside the rest of the eBus ecosystem — the [Electrification Bus specification](https://github.com/electrification-bus/specification), [`python-sdk`](https://github.com/electrification-bus/python-sdk), and [`ebus-mqtt-client`](https://github.com/electrification-bus/ebus-mqtt-client) — see the specification repo's README §Governance for the project's long-term governance context.
