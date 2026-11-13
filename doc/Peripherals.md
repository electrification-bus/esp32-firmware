# Peripherals

A peripheral is hardware that a project's own `src/` and the firmware's features use directly, in any role (`device`, `controller` or `standalone`). A `device.yml` lists it under `peripherals:`, and `generate` declares it as a static object named after its id. A node, listed under `nodes:`, is a driver that publishes Homie properties; a peripheral publishes nothing, so the app on the board decides what it shows or does. A component is one or the other: `generate` refuses a peripheral component under `nodes:`, and a node component under `peripherals:`.

The first peripheral driver is `spi/ili9341`, a 320x240 ILI9341 TFT such as the Olimex MOD-LCD2.8RTP.

## Example

An Olimex MOD-LCD2.8RTP on the UEXT connector of an ESP32-POE-ISO, showing the OpenADR 3 price:

```yaml
buses:
  spi: { pin_sck: 14, pin_mosi: 2 }

peripherals:
  - id: lcd
    component: spi/ili9341
    config: { pin_cs: 5, pin_dc: 15 }

oa3:
  base_url: https://price.grid-coordination.energy/openadr3/3.1.0
  program_name: EELEC-012041131
  display: lcd
```

The panel is write-only, so `buses.spi` has no `pin_miso`: the module takes UEXT pin 7, MISO's pin (GPIO15), as its D/C line. `oa3.display` puts the VEN's price screen on it ([OpenADR3.md](OpenADR3.md#price-screen)).

`generate` writes `.ebus/src/peripherals.h`:

```cpp
#pragma once

#include <peripheral/peripheral.h>
#include <spi/ili9341.h>

extern Ili9341 lcd;
```

and `.ebus/src/peripherals.cpp`, with the object and the table that lookups by id search:

```cpp
#include "peripherals.h"

Ili9341 lcd("lcd", 14, -1, 2, 5, 15, 1, 40000000);

Peripheral* const peripheral_table[] = {&lcd};
const size_t peripheral_count = 1;
```

Both files are written on every `generate`, empty of peripherals when `device.yml` lists none.

## `peripherals:` entries

| Field | Required | Description |
|---|---|---|
| `id` | yes | A Homie id: lower-case letters, digits and `-`, starting with a letter or digit. The C++ object is named after it with `-` mapped to `_`. Peripheral and node ids share one namespace, so an id may not repeat one under `nodes:` |
| `component` | yes | `<bus>/<name>`, a `lib/<bus>/<name>.yml` whose yml has a `peripheral:` block, from this repo or the project's own `lib/` ([Project components](#project-components)) |
| `config` | no | Values for the component's constructor template, over its `config_defaults`. As with a node, every `{config.KEY}` the template uses is required and a key it does not use is refused |

Bus pins come from the top-level `buses:` block, as for nodes. Every `pin` and `pin_*` value, in `config:` or on a bus the component uses, joins the generator's check that no GPIO is assigned twice.

`main.cpp` calls `setup_peripherals()` early in `setup()` in every role, before the network starts, and `loop_peripherals()` on each pass of `loop()`. Each runs the peripherals' `setup()` or `loop()` in `device.yml` order (`src/peripheral/peripheral.cpp`).

## Using a peripheral

### From a project's `src/`

Include the generated header and use the object by its id's C++ name. The compiler checks the name and type:

```cpp
#include <peripherals.h>

void draw_banner() {
    PeripheralLock l(lcd);                       // one redraw, not interleaved with another task's
    lcd.clear(Display::kBlack);
    lcd.text(8, 8, "Hello", 3, Display::kWhite, Display::kBlack);
    lcd.flush();
}
```

`native()` returns the driver's own library object for drawing beyond the `Display` interface; for `Ili9341` it is the `Adafruit_ILI9341` (an `Adafruit_GFX`). Hold a `PeripheralLock` while using it, since calls on the library object bypass the peripheral's mutex:

```cpp
PeripheralLock l(lcd);
lcd.native().drawCircle(160, 120, 40, Display::kGreen);
```

A project that uses a library of its own in `src/` adds it with [`lib_deps:`](#lib_deps).

A project's `src/` gets two optional hooks in every role, declared in `include/app.h`: `setup_app()` once at the end of `setup()`, with the peripherals and features up, and `loop_app()` on every pass of `loop()`, on the loop task. The firmware's defaults (`src/main.cpp`) do nothing:

```cpp
#include <app.h>
#include <peripherals.h>

static void on_touch(void*, const TouchState& s) {
    if (s.pressed) Serial.printf("touched at %d,%d\n", s.x, s.y);
}

void setup_app() {
    touch.on_touch(on_touch, nullptr);
}
```

### From a feature

Firmware code built for every project cannot include `<peripherals.h>`, so it looks a peripheral up by id and type:

```cpp
#include <peripheral/display.h>

Display* d = peripheral<Display>("lcd");   // nullptr when device.yml lists no display "lcd"
```

`peripheral<T>(id)` searches the generated table for an entry with that id and `T`'s kind. The OpenADR 3 VEN does this with the id in `oa3.display` (`src/platform/oa3_port.cpp`).

## The `Display` interface

`include/peripheral/display.h` is the drawing interface every display driver implements over its own graphics library, so code that finds a display by id draws on any panel.

| Method | Does |
|---|---|
| `width()`, `height()` | The panel's size in pixels, after rotation |
| `rotation()` | 0-3, quarter turns from the panel's native orientation |
| `clear(color)` | Fills the screen |
| `fill_rect(x, y, w, h, color)` | Fills a rectangle |
| `text(x, y, s, size, color, bg)` | Text with its top-left corner at `x,y`, on a `bg`-colored box, in the library's built-in font; size 1 is its base size and larger sizes multiply it |
| `flush()` | Sends buffered drawing to the panel; a driver that draws straight to the panel (`Ili9341`) does nothing here |

Colors are RGB565. A monochrome panel lights any nonzero color. The interface names `kBlack`, `kWhite`, `kRed`, `kGreen`, `kBlue`, `kYellow` and `kGrey`.

## Touch

`include/peripheral/touch.h` is the interface of a touch panel over a display. The first driver is `i2c/ns2009`, the touch controller of an Olimex MOD-LCD2.8RTP from revision C on:

```yaml
buses:
  spi: { pin_sck: 14, pin_mosi: 2 }
  i2c: { pin_sda: 13, pin_scl: 33 }        # ESP32-POE-ISO-WROVER UEXT

peripherals:
  - id: lcd
    component: spi/ili9341
    config: { pin_cs: 5, pin_dc: 15 }
  - id: touch
    component: i2c/ns2009
    config: { display: lcd }
```

The driver's `loop()` samples the panel every `sample_ms` (default 20) and counts a press or a release after two agreeing samples. It maps raw readings to the display's pixels through its calibration and the display's current `rotation()`, so `x` and `y` match what was drawn there. A held finger's position is taken one sample late, so the last reading before lift-off, which can pass the pressure threshold with a coordinate near an edge, never becomes the reported position.

| Call | Does |
|---|---|
| `state()` | The latest `TouchState`: `pressed`, `x`, `y` (the last position while pressed, the release position after) and `since_ms` (`millis()` of the last press or release). No bus traffic; any task |
| `on_touch(fn, ctx)` | Registers `void fn(void* ctx, const TouchState&)`, called on each press and each release, on the loop task, with the touch peripheral locked |

`i2c/ns2009`'s `config:` keys, with Olimex's values as defaults (`lib/i2c/ns2009.yml`):

| Key | Default | |
|---|---|---|
| `display` | (required) | The id of the display the panel covers; `generate` refuses one that names no display |
| `address` | `0x48` | |
| `x_min`, `x_max`, `y_min`, `y_max` | 200, 3850, 400, 3920 | Raw readings at the panel's edges |
| `swap_xy`, `invert_x`, `invert_y` | false, false, true | How the panel's axes sit against the display's native (rotation 0) orientation |
| `sample_ms` | 20 | |
| `z_threshold` | 150 | The pressure above which the panel counts as pressed |

A peripheral component names the config keys that refer to other peripherals in its `peripheral:` block, `references: { display: display }` for this one; `generate` checks each names a peripheral of that kind.

## Concurrency

Any task may call a peripheral. Each `Peripheral` holds its own recursive FreeRTOS mutex (`include/peripheral/peripheral.h`), and every `Display` method takes it for the call. A sequence of calls that must not interleave with another task's, such as a redraw, holds a `PeripheralLock` for its scope; the calls inside take the mutex again, which a recursive mutex allows. `setup_peripherals()` and `loop_peripherals()` hold the lock around each peripheral's `setup()` and `loop()`.

Transactions on a shared I2C or SPI bus are serialized by the framework's HAL locks.

## Writing a peripheral component

A peripheral component is the same three files as a node component, `lib/<bus>/<name>.{h,cpp,yml}`, with a different base class and a `peripheral:` block in the yml. `lib/spi/ili9341.*` is the reference.

The class derives from the interface for its kind (`Display` for a display) and implements its `do_*` methods; the public methods in the base take the mutex and call them. It overrides `setup()` for the hardware bring-up, which starts its bus itself (`ensure_spi_bus()` in `Ili9341::setup()`), and `loop()` when it has periodic work. It offers `native()` returning its library's object. The constructor does no hardware work.

The yml has no Homie `nodes:`; it has the `peripheral:` and `generator:` blocks:

```yaml
peripheral:
  kind: display
generator:
  class: Ili9341
  header: spi/ili9341.h
  constructor: '("{id}", {bus.spi.pin_sck}, {bus.spi.pin_miso|-1}, {bus.spi.pin_mosi}, {config.pin_cs}, {config.pin_dc}, {config.rotation}, {config.spi_hz})'
  config_defaults:
    rotation: 1
    spi_hz: 40000000
  lib_deps:
    - adafruit/Adafruit BusIO @ 1.17.4
    - adafruit/Adafruit GFX Library @ 1.13.0
    - adafruit/Adafruit ILI9341 @ 1.7.0
```

| Key | Description |
|---|---|
| `peripheral.kind` | What the class implements; `display` is the kind so far (`PeripheralKind::kDisplay`). A feature that takes a peripheral checks it: `oa3.display` must name a `display` |
| `generator.class`, `header`, `constructor` | Required, as for a node component ([GenerateHomieDevice.md](GenerateHomieDevice.md#constructor-template-syntax)) |
| `generator.config_defaults` | Values for `{config.KEY}` that `device.yml` may leave out |
| `generator.lib_deps` | The libraries the driver needs, each at an exact version ([Library versions](#library-versions)) |

### Optional bus keys

`{bus.<bus>.<key>|<default>}` in a constructor template makes a bus key optional: when `buses.<bus>` leaves the key out, the text after `|` is inserted as written. `spi/ili9341` takes `{bus.spi.pin_miso|-1}`, so a write-only panel needs no MISO pin. A component that reads the bus keeps the plain `{bus.spi.pin_miso}`, which stays required (`spi/atm90e32_energy_meter`).

## Project components

A project may hold components of its own in `lib/<bus>/<name>.{h,cpp,yml}`, laid out like the repo's `lib/`, and use them under `peripherals:` or `nodes:` like the repo's. `generate` stages each `lib/<bus>/` of the project as a PlatformIO library under `.ebus/lib/`, so includes are `<bus>/<name>.h`, and only the components the build uses are compiled. `examples/bms` keeps its `ble/serial_bms` node component this way, in `examples/bms/lib/ble/`.

A project component may not share its `<bus>/<name>` with a component in this repo's `lib/`; `generate` refuses the build and names the clash.

## `lib_deps:`

A top-level `lib_deps:` list adds PlatformIO libraries for the project's own `src/`:

```yaml
lib_deps:
  - adafruit/Adafruit NeoPixel @ 1.12.3
```

### Library versions

`generate` gathers the `lib_deps` of every component the build uses (including a component compiled only because another one includes its header), the project's `lib_deps:`, and cpp-oa3 for an `oa3:` block, and checks them (`check_lib_deps()` in `ebus-esp32`):

- **Every entry names an exact version:** `owner/Name @ 1.2.3`, a git URL ending in `#<tag or commit>`, or a local `symlink://` or `file://` source. A bare name with no owner and no version, such as `Wire`, is a framework library. A range such as `^1.2.0` or `~1.2.0` is refused.
- **A library is asked for at one version.** Two entries for the same library at different versions are refused, with both named, rather than left for PlatformIO to pick one. Entries match by the library's name, ignoring case and anything but letters and digits, so `adafruit/Adafruit BusIO @ 1.17.4` and `https://github.com/adafruit/Adafruit_BusIO.git#v1.17.4` are the same library at the same version. A leading `name=` in an entry names the library.
