# Extracting the Homie layer and drivers as a standalone library

**Status: proposal. Stages 0, 1 and 3 implemented, and Stage 4 in part; Stages 2 and 5 not built.** Stages 1, 3 and 4 landed in a different shape from the one below: the transport is an abstract class, `HomieTransport`, the log and clock hooks are separate headers, and the core lives in `lib/ebus_core` with host tests in `test/native/`. [`lib/ebus_core/README.md`](../../lib/ebus_core/README.md) describes what landed and what a port implements. Like the rest of `doc/proposals/`, this records a design and its evidence so the decision survives independently of the conversation that produced it. Mark stages `implemented` as they land, the way [DriverLayering.md](DriverLayering.md) does.

## The problem

Everything reusable in this repo — the Homie 5 protocol implementation and the `NodeEntity` driver model — is reachable only by cloning the whole firmware project and building it with PlatformIO. There is no way to use the protocol layer in another project, no way to compile a driver under CMake or ESP-IDF, and no way to unit-test any of it off a board.

The goal is a **separate library, in its own repository, consumable from any CMake-based build**, with a host unit-test target as its first real consumer.

Scope is deliberately two things: **the `lib/` drivers and devices, and the Homie convention layer** (`src/homie/`, `src/node/`, `include/homie/`, `include/node/`). Everything in `src/platform/` other than the MQTT client and the log tee — network, config, OTA, captive portal, NVS prefs — stays application code. So does `src/main.cpp`.

The `device.yml` → `ebus-esp32 generate` pipeline is **out of scope**, and is not a compile-time dependency: `scripts/generate_homie.py:22` stands aside when there is no `device.yml`, and [HandCodedDevice.md](../HandCodedDevice.md) documents a fully supported hand-written `src/homie.cpp`.

## The coupling is smaller than the include graph suggests

Measured, not estimated. This is the finding that makes the whole exercise cheap:

| Coupling | Actual size |
|---|---|
| `MQTTClient` surface outside `src/platform/mqtt_client.cpp` | **25 call sites**, only `publish`, `subscribe`, `connected` |
| Arduino *runtime* calls in `src/homie` + `src/node` | **6** — `millis()` ×4, all in `controller.cpp`; `delay()` ×2, `Property.cpp:282,284` |
| `<ArduinoYaml.h>` in the Homie layer | **Dead** (removed in Stage 0). Was included at `Node.cpp:1` and `Device.cpp:2`, zero YAML symbols used |
| `<platform/config.h>` in `Device.cpp:6` | **Dead** (removed in Stage 0). No config global referenced |
| PlatformIO-specific preprocessor macros | **Zero** — `grep -rn 'PLATFORMIO\|PIOENV' src include lib` → no hits |
| `Serial.` sites in library-bound code | 159 (58 in the core, 101 in drivers) |

Two further details matter:

- `include/platform/mqtt_client.h` **forward-declares** `class Property` rather than including it, so `include/node/NodeProperty.h:9`'s include of that header is *only* for the `entity_settable_callback_t` / `settable_handler_t` typedefs. The comment at `NodeProperty.h:2-3` claiming it "carries homie/Property.h" is stale and should be fixed.
- The `millis()` calls are confined to `controller.cpp`, which is behind `ENABLE_CONTROLLER` — currently commented out in `platformio/platformio.ini`. The mainline core needs only `delay()`.

**The blockers are header hygiene, not architecture.** `<MQTT.h>` and `<Arduino.h>` leak into public headers (`Node.h:3`, `controller.h:2,4`, `mqtt_client.h:2,4`), and that leak — not any real design entanglement — is what makes the layer unusable elsewhere.

Already framework-free today, compiling against libc alone: `homie.h`, `homie_enums.h`, `homie_descriptor.h`, `homie_datatype.h`, `homie_id.h`, `homie_globals.h`, `ebus_vocabulary.h`, `mdns_strings.h`, `homie_datatype.cpp`, `homie_id.cpp` (now in `lib/ebus_core/`). The last two have been compiled and unit-tested with Apple clang on macOS, so the portable core is not hypothetical.

## Library boundary — three artifacts

Split on what each is allowed to `#include`:

| Artifact | Contents | May include | External deps |
|---|---|---|---|
| **homie-core** | Homie 5 protocol + the driver contract | libc, C++17 headers, ArduinoJson | ArduinoJson |
| **homie-arduino** | the port implementations | Arduino, arduino-mqtt, ESP-IDF | arduino-mqtt, arduino-esp32 |
| **homie-drivers-\<bus\>** | one per `lib/<bus>/` | Arduino + homie-core | per bus: ModbusMaster, Adafruit_BME280, ATM90E32, NimBLE |

Driver grouping stays exactly as it is today — `gpio`, `sim`, `i2c`, `modbus`, `spi`, `ble`, plus today's `lib/platform/modbus/` as `homie-bus-modbus`. Each ships a transport + bus entity + its components, which is already how the directories are arranged. This keeps the three-layer vocabulary from [DriverLayering.md](DriverLayering.md) intact:

- **transport** — `i2c_bus`, `spi_bus`, `modbus_master`/`modbus_queue`, `ble_client`
- **bus entity** — `I2CEntity`, `SPIEntity`, `ModbusRTUEntity`
- **component** — `BME280`, `EnergyMeter`, `SHT20`, `DigitalOutput`, …

`include/util/jsonUtils.h` is a single ArduinoJson helper used by `Device.cpp`, `homie_json.cpp` and `NodeEntity.cpp`. Fold it into `homie/homie_json.h` rather than shipping a `util/` namespace that will collide with a consumer's.

## The three seams

### `include/homie/homie_mqtt.h` — the transport port

House style throughout: plain function pointers plus an opaque `void* ctx`, never `std::function`.

```cpp
struct HomieMqttPort {
    void* ctx;
    bool (*publish)      (void* ctx, const char* topic, const char* payload,
                          int length, bool retained, int qos);
    bool (*queue_publish)(void* ctx, const char* topic, const char* payload,
                          int length, bool retained);   // task-safe path
    bool (*subscribe)    (void* ctx, const char* topic, int qos);
    bool (*connected)    (void* ctx);
};
void                 homie_mqtt_bind(const HomieMqttPort* port);
const HomieMqttPort* homie_mqtt();      // callers stay null-safe
```

`queue_publish` is a **separate slot on purpose.** `Property::publish_queued` goes through `mqtt_queue_publish` because drivers call it from BLE and Modbus tasks, while `publish_value` is main-task direct. Collapsing the two would silently change threading behavior — the kind of regression that shows up as rare corruption, not a failed build.

`mqtt_qos` and `homie_qos()` move here from `platform/mqtt_client.h:93-94`: they are Homie policy currently stranded in an Arduino header.

`MQTTClient*` becomes `const HomieMqttPort*` in `Device.h:139`, `Node.h:53`, `Property.h:119` and `controller.h`. Keep the method names (`setMQTTClient`) so the diff stays a type change rather than a rename.

### `include/homie/homie_log.h` — the console seam

- `homie_logf(fmt, ...)` with a bindable sink (fn-ptr + ctx), defaulting to `vfprintf(stdout, …)` — which works on ESP-IDF and in a host test binary.
- `homie_millis()` / `homie_delay_ms()`, covering the six Arduino runtime calls.
- A compatibility shim, so none of the 159 `Serial.` call sites change:

```cpp
#if __has_include(<Arduino.h>)
  #include <platform/log_stream.h>     // keeps the existing #define Serial logStream
#else
  // minimal HomieConsole with printf/print/println forwarding to homie_logf
  #define Serial homieConsole
#endif
```

`Serial.printf` is the documented house idiom (`log_stream.h:13`); rewriting 159 sites to remove a token the project deliberately uses would be churn for its own sake. The shim is ~40 lines and gets the same portability.

`include/node/NodeEntity.h:6` then includes `<homie/homie_log.h>` instead of `<platform/log_stream.h>`. **This one line is the riskiest change in the proposal** — see Risks.

### `include/homie/homie_settable.h` — the settable registry, moved

Move out of `src/platform/mqtt_client.cpp` (~lines 154–625), verbatim: `subscribed_settable_property_t`, the callback typedefs, `subscribe_for_callbacks`, `entity_subscribe_for_callbacks`, `subscribe_settable_handler`, the dispatch loop and `deliver_local_set`.

That block is already framework-free — `strcmp`/`strcpy` and function pointers, plus `Serial.printf` diagnostics and exactly one Arduino touch (`mqtt_client.subscribe` in the resubscribe loop). Add `homie_settable_count()` and `homie_settable_topic(int)` so the adapter's connect loop can drive resubscription from outside.

This is the highest-leverage single change here: it deletes the MQTT dependency from the driver contract outright. Two invariants must survive it — the state-dependent `Property*` vs name-based overload rule, and the null-initialized callback fields that fixed the `$state` crash.

## Staging — decouple in place, split last

Every decoupling stage is verified by the **existing PlatformIO build**. The repo split comes last, as a mechanical transplant of already-clean code. Splitting first would mean debugging a new build system and a new boundary simultaneously.

| Stage | Work | Size |
|---|---|---|
| **0** | Delete the dead includes listed above; fix the stale `NodeProperty.h` comment. **Implemented** — `<ArduinoYaml.h>` also turned out dead in `mqtt_client.cpp` and the public `config.h`; it now appears only in `config.cpp`, the one real YAML user | 1–2 h, zero risk |
| **1** | Move the settable registry into the core; repoint `NodeProperty.h:9`; keep `mqtt_client.h` re-exporting via `inline` forwarders. **Ends `<MQTT.h>` in the driver contract.** **Implemented** (`homie/homie_settable.h`, caller-supplied storage) | 1 day |
| **2** | Add `homie_log.h` with the shim; repoint `NodeEntity.h:6`. **Driver contract then compiles without Arduino** | ½ day |
| **3** | Add `homie_mqtt.h`; swap the pointer type in 4 headers and ~25 bodies; implement the port in `mqtt_client.cpp`. **Core then compiles against libc + ArduinoJson alone.** **Implemented** (`homie/homie_transport.h`) | 2 days |
| **4** | `CMakeLists.txt`, `cmake/homie-core.cmake`, `test/cpp/` with a fake port that records publishes. **In part**: `lib/ebus_core/CMakeLists.txt`, and `test/native/` suites through `FakeTransport` | 1–2 days |
| **5** | `git subtree split` / `filter-repo` into its own repo, history preserved | 1 day |

Roughly **5–7 days**, ~130 edits plus one 470-line file move. No stage is algorithmically hard; the compiler finds every site in Stage 3.

### Stage 4 is the point, not a formality

After Stage 3 the testable surface includes `Property`, `Node`, `Device`, `homie_settable` and `NodeProperty` — which means the two trickiest pieces of logic in the tree finally get tests:

- the **publish-change gate** (`Property.h:92-101`) — retained vs non-retained vs `force`, and retraction clearing the memo
- the **settable dispatch precedence** — the overload rule that depends on whether MQTT was connected at subscribe time

Neither has a test today. A fake `HomieMqttPort` recording `{topic, payload, retained}` is all it takes, and the port exists precisely to make that possible.

### Stage 5 layout

```
include/homie/…  include/node/…    src/homie/…  src/node/…    drivers/<bus>/…
CMakeLists.txt        # any CMake consumer
library.json          # PlatformIO consumer — same tree, no conflict
test/cpp/             # host tests
```

One tree carries `CMakeLists.txt` and `library.json` simultaneously; they do not collide. esp32-sdk consumes it back as a git submodule plus `lib_extra_dirs` during co-development (editable in place), switching to a versioned `lib_deps` git URL once it stabilizes. Every `drivers/<bus>/library.json` must stay byte-identical — the `"includeDir": ".."` trick is what makes `#include <modbus/sht20.h>` resolve.

## Risks

**The `NodeEntity.h:6` include is load-bearing in a non-obvious way.** It is what opts 101 driver `Serial.` sites into the TCP tee. A subtly wrong `__has_include` guard in `homie_log.h` stops the tee with **no compile error** — drivers would simply go quiet on port 23 and nobody would notice for months. Mitigation: preprocess one driver translation unit per stage and grep for `logStream`.

**PlatformIO's LDF reacts to include changes.** `lib_ldf_mode = deep+` decides what gets compiled *and* which dependencies get installed by scanning includes. Adding `<homie/homie_settable.h>` to `NodeProperty.h` changes what it scans. It should be a no-op since everything stays under `include/`, but verify with a `compile_commands.json` diff rather than assuming.

**`lib/sim/` has no `library.json`** and resolves `<sim/...>` only because `scripts/add_project_include.py:44` puts `lib/` on the include path. It needs one before the move, or it breaks in Stage 5.

**The generator's component scan must follow the drivers.** `find_component_yml()` in `ebus-esp32` scans `lib/<bus>/<name>.yml` only; once drivers live in the submodule, `LIB_DIR` becomes a short search list. One constant — but it *is* required for `component: modbus/sht20` to keep resolving. Noted because code generation was out of scope, not because it can be skipped.

**Driver selection becomes explicit.** PlatformIO infers it from include scanning; CMake consumers need a `HOMIE_DRIVERS` list. This is an improvement — it removes the trap [DriverLayering.md](DriverLayering.md) documents, where putting BLE infrastructure under `lib/platform/ble/` would make every BLE build drag in ModbusMaster.

## Deliberately not doing

- **Arduino IDE / arduino-cli packaging.** The 1.5 library spec wants `library.properties` and a flat `src/` tree, which conflicts with this layout, for an audience that overlaps PlatformIO users almost entirely. If it is ever wanted, generate it as a release artifact from one source of truth rather than distorting the repo.
- **Rewriting the 159 `Serial.` sites to `homie_logf`.** The shim gets ~95% of the benefit for ~3% of the cost.
- **Making the core ArduinoJson-free.** `$description` publishing is not optional in Homie 5, and ArduinoJson is header-only, framework-agnostic, and available as both an ESP-IDF component and an Arduino library. Declare it and move on.
- **ESP-IDF component manifests.** Not needed for the chosen target; a thin `idf_component.yml` wrapper over the CMake targets is a small follow-on whenever an IDF consumer appears. arduino-esp32 is itself an IDF component, so such a consumer keeps every driver unchanged.

## Verification

Per stage, in order:

1. `./ebus-esp32 build` — green at every stage.
2. `./ebus-esp32 compiledb`, then diff `compile_commands.json` flag sets — catches an LDF reaction and confirms no include path moved.
3. Stage 2 only: preprocess a driver TU, grep for `logStream` — proves the tee survived.
4. Stage 4: `cmake -B build-host && cmake --build build-host && ctest` — passes on a Mac with no ESP32 attached.
5. Stage 5: build esp32-sdk against the submodule, flash the POE-ISO, confirm a node still publishes and a `/set` still actuates — end-to-end proof that the change gate and the settable dispatch came through the move intact.

## Decisions on record

- **Target consumer: plain CMake**, with host unit tests as the first one. Not ESP-IDF component manifests, not Arduino IDE.
- **Own repository**, not an in-repo restructure and not manifests over the existing tree.
- **Compatibility shim** for the `Serial` seam, not a `homie_logf` migration.
