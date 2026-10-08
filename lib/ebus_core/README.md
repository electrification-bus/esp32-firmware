# ebus_core

The portable part of the eBus / Homie 5 SDK: code that depends on the C and C++ standard libraries only, with no Arduino, ESP-IDF or FreeRTOS header. It is the seed of a standalone C++ SDK for other MCUs and operating systems (Zephyr, FreeRTOS on STM32 or NXP, embedded Linux).

| Header | Contents |
|---|---|
| `homie/homie.h` | Homie version, topic domain and prefix, datatype and attribute strings |
| `homie/homie_limits.h` | Longest ids and topics; every buffer is sized from these. `./ebus-esp32 generate` reads this file to check `device.yml` |
| `homie/homie_enums.h` | `PropertyDatatype` and `Unit` enums and their Homie strings |
| `homie/homie_descriptor.h` | `PropertyDesc`, the declarative property descriptor |
| `homie/homie_id.h` | Id sanitizing, MAC-based ids, id templates, child ids |
| `homie/homie_datatype.h` | Payload validation and parsing for every Homie datatype |
| `homie/controller_inbox.h` | Controller message inbox (caller-owned arena) and topic parser |
| `homie/ebus_vocabulary.h` | Registered eBus device and capability type strings |
| `platform/broker_discovery.h` | Broker service names, ports and discovery list |
| `platform/mdns_strings.h` | eBus mDNS service types and TXT keys |

## Rules for code in the core

- Include only standard headers and other core headers. CI enforces this.
- No dynamic allocation, no Arduino `String`, no STL containers, no lambdas: callers pass buffers.
- C++17.

## Building

**PlatformIO** links it automatically: `library.json` makes `lib/ebus_core` a library, and the dependency finder adds it to any build that includes one of its headers. `[env:native]` lists it in `lib_deps` for the host unit tests in `test/native/`.

**CMake**, for any other build system or host:

```bash
cmake -S lib/ebus_core -B lib/ebus_core/build
cmake --build lib/ebus_core/build
```

A parent project uses `add_subdirectory(<path>/ebus_core)` and links the `ebus_core` target, which carries its include directory and C++17. `-DEBUS_CORE_EBUS_TOPIC=ON` defines `USE_EBUS_TOPIC`, which moves the topic root from `homie/5` to `ebus/5`; the ESP32 firmware sets it in `platformio.ini`. Built as the top-level project, the target `ebus_core_header_check` also compiles each public header in a translation unit of its own.

CI (`core-host-build` in `.github/workflows/build.yml`) runs that CMake build with GCC and Clang, warnings as errors, and only the core's include directory on the path.

## Include paths

The headers keep the paths they had under the project's `include/` (`<homie/homie_id.h>`, `<platform/broker_discovery.h>`), so no `#include` in the firmware changed when they moved. `platform/` is the wrong name for the two discovery headers in a portable library; rename it (to `ebus/`, say) in one sweep when the core becomes its own repository.

## Next step: hooks and /set dispatch

The Homie object model (`Device`, `Node`, `Property`) and the MQTT client stay in `src/` because they call the platform directly. Moving them into the core takes:

- **Transport hook**: a small struct of function pointers (publish, subscribe, unsubscribe) that `Device` and `Property` call instead of `include/platform/mqtt_client.h`; the ESP32 implementation wraps the existing MQTT client and its publish queue.
- **Clock hook**: a millisecond tick function in place of `millis()` (`src/homie/controller.cpp`), and a non-blocking replacement for the `delay()` calls in `src/homie/Property.cpp`.
- **Log hook**: a `printf`-style function in place of `Serial.printf()`, which the ESP32 build points at the UART and TCP log tee.
- **/set dispatch**: the settable table (each `/set` topic with its handlers and owning `Property` or `NodeEntity`) and `dispatch_settable()` live in `src/platform/mqtt_client.cpp`. They are Homie logic, not transport, and belong in the core, with the table storage supplied by the caller (it is allocated with `new` today) so the core stays heap-free.
