# ESP32 OTA (Over-The-Air) Updates

How firmware and filesystem images reach a device that has no reachable USB cable.

## Status at a glance

| Mechanism | Direction | State |
|---|---|---|
| `POST /api/ota/firmware`, `/api/ota/filesystem` | push (host → device) | **implemented** |
| `firmware_url` / `fs_url` download | pull (device → server) | **implemented** |
| `ebus-esp32 push --ota` | push, both images, one command | **implemented** |
| `ebus-esp32 push --pull --url …` | pull, device downloads from your server | **implemented** |
| `ebus-esp32 serve` | serves the built images for a pull (dev convenience) | **implemented** |
| ArduinoOTA / espota, port 3232 | push | **removed** — see [below](#why-arduinoota-espota-was-removed) |

---

## Push over TCP (recommended for development and field work)

The device's portal accepts a raw image body and streams it straight into the app or
filesystem partition. Implemented in the "OTA image push" block of
[`src/platform/captive_portal.cpp`](../src/platform/captive_portal.cpp).

### Endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET`  | `/api/ota` | status of the *pull* updater (idle/downloading/error detail) |
| `POST` | `/api/ota` | enable/disable the OTA window — `{"enabled":true}` |
| `POST` | `/api/ota/firmware` | push an app image (`U_FLASH`) |
| `POST` | `/api/ota/filesystem` | push a LittleFS image (`U_SPIFFS`) |

All four routes are registered with `AsyncURIMatcher::exact()`, and that is load-bearing.
AsyncWebServer's default matcher is `BackwardCompatible` — `^{uri}(/.*)?$` — so a plain
`server.on("/api/ota", ...)` *also* matches `/api/ota/filesystem`, and `_attachHandler()`
binds the first handler that matches. Registered plainly, the toggle silently swallows
every image push: the body lands in `accumulateBody`'s 2 KB static and the image is
discarded. The symptom is a `PORTAL: accumulateBody url=/api/ota/filesystem` line in the
log where `PORTAL: OTA push started` belongs.

**Query parameter:** `?reboot=0` stages the image and keeps the device running, so both
images can be pushed in one session with a single restart at the end. The default is to
reboot, so a one-image `curl` takes effect immediately.

### With the CLI

```sh
ebus-esp32 push --ota --host 192.168.1.42   # firmware + filesystem, one reboot
ebus-esp32 push --ota                       # host learned from a prior USB session
ebus-esp32 push --ota --fs-only             # config only (reboots itself)
ebus-esp32 run  --ota --monitor             # build, flash over TCP, then watch it boot
ebus-esp32 push --ota --host 10.0.0.5:8080  # non-default port
```

The CLI opens the OTA window, pushes the filesystem staged, then the firmware carrying
the reboot. The passphrase is `--passphrase` if given, else `device.device_passphrase`
from `config-overlay.yml`, else from `device.yml`. See [GettingStartedCLI.md](GettingStartedCLI.md).

### With curl

```sh
curl -u admin:changeme -X POST -d '{"enabled":true}' http://192.168.1.42/api/ota
curl -u admin:changeme --data-binary @.pio/build/<env>/littlefs.bin \
     "http://192.168.1.42/api/ota/filesystem?reboot=0"
curl -u admin:changeme --data-binary @.pio/build/<env>/firmware.bin \
     http://192.168.1.42/api/ota/firmware
```

Send credentials **pre-emptively** (as `-u` does). An auth scheme that waits for a 401 and
retries will upload the whole image twice.

### What the implementation guarantees

- **Streamed, never buffered.** The body handler writes each chunk into `Update.write()`.
  It deliberately does not use `accumulateBody` — that buffers into a 2 KB static, and an
  app image is >1 MB.
- **Authorized on the first chunk.** Body callbacks run *before* the request handler, so
  authorizing only there would let an unauthenticated client on the LAN stream an entire
  image into flash before being refused. A side-effect-free `portalAuthorized()` predicate
  gates the writes; `requireAuth()` still produces the actual 401/429 response afterward.
  (Two responses to one request is a bug, which is why the check is split in two.)
- **Reboot deferred to the main loop.** The handler runs on the AsyncTCP task and the
  response has not reached the wire yet; `captive_portal_loop()` performs the restart.
- **Filesystem safety.** `U_SPIFFS` requires unmounting LittleFS first. If the push is
  staged or fails, LittleFS is remounted so the device stays serviceable rather than
  needing a power cycle.
- **Rejected up front:** OTA window closed, zero/absent `Content-Length` (the device sizes
  `Update.begin()` from it), a concurrent push, and — for app images — a first byte that
  is not the ESP image magic `0xE9`.

### Stack sizing

Flash writes happen on the AsyncTCP service task, not the main loop, so the task's stack
has to cover them. **It already does** — `CONFIG_ASYNC_TCP_STACK_SIZE` is left at the
library default (16384 in the ESP32Async fork of AsyncTCP; upstream is 8192).

Measured on an ESP32-S3, both image types, at the 16384 default:

```
PORTAL: OTA push started  (filesystem,  196608 bytes), async_tcp stack: 13796 bytes free of 16384 before writes
PORTAL: OTA push complete (196608 bytes written),      async_tcp stack: 13796 bytes free of 16384 at low-water (used 2588)
PORTAL: OTA push started  (firmware,  1353856 bytes),  async_tcp stack: 13796 bytes free of 16384 before writes
PORTAL: OTA push complete (1353856 bytes written),     async_tcp stack: 13796 bytes free of 16384 at low-water (used 2588)
```

Peak usage is **2,588 bytes** — identical for a 192 KB filesystem and a 1.35 MB firmware,
and identical before and after each transfer. Two things follow:

- **The write path costs no stack.** The peak is set by ordinary TCP/lwIP processing
  before the first `Update.write()`. That includes the `U_FLASH` finalization
  (`Update.end(true)` → `esp_ota_set_boot_partition()` → `esp_image_verify()`), which the
  filesystem path never runs.
- **Image size is irrelevant.** Each chunk traverses the same call path, so a larger image
  means more calls at the same depth, not deeper ones.

That leaves ~84% of the default stack unused, which is why there is no override.

The probe stays in `otaPushBody()`. Re-measure before trusting these numbers with flash
encryption enabled (the partition layer encrypts through a stack buffer) or with a TLS
listener added (an mbedTLS handshake alone wants ~20 KB). If a bump is ever needed it must
be a `build_flag`, not a `build_src_flag` — the value is consumed when AsyncTCP compiles,
and `build_src_flags` only reaches `src/`. A watchdog reset or a
`stack canary watchpoint triggered` panic mid-push is the failure mode to watch for.

### Side effect: the main loop stalls during a push

Flash writes are slow and block the task performing them, and the main loop gets less time
while a large image streams in. On a board with Modbus nodes this shows up as timeouts:

```
PORTAL: OTA push started (firmware, 1353856 bytes) ...
Modbus: Cmd 7 failed - RESPONSE_TIMEOUT (0xE2) addr=1 reg=1
SHT20: Async read failed, error=226
```

Harmless for a firmware push — the device reboots immediately afterward. For a *staged*
filesystem push (`?reboot=0`) the device keeps running, so expect a gap in readings rather
than a failure. Don't push images to a device whose sensor data is being relied on at that
moment.

The probe stays in `otaPushBody()`. Re-measure before trusting these numbers with flash
encryption enabled (the partition layer encrypts through a stack buffer) or with a TLS
listener added (an mbedTLS handshake alone wants ~20 KB). If a bump is ever needed it must
be a `build_flag`, not a `build_src_flag` — the value is consumed when AsyncTCP compiles,
and `build_src_flags` only reaches `src/`. A watchdog reset or a
`stack canary watchpoint triggered` panic mid-push is the failure mode to watch for.

---

## Pull from a URL (suited to fleet rollout)

The device downloads the images itself. This is the shape that works when the device can
reach a server but you cannot reach the device — a fleet behind a NAT or gateway — and
the natural fit for rolling a versioned artifact out to many boards. Implemented in
[`src/platform/ota.cpp`](../src/platform/ota.cpp).

### Serving the images

The image server is yours to run and configure — a release server, an S3 bucket, an
nginx on the LAN. For development, `serve` publishes the current build without setting
anything up:

```sh
./ebus-esp32 serve                 # http://<this machine>:8080
./ebus-esp32 serve --port 8443 --cert server.pem --key server.key    # https
```

```
--- serving .pio/cli/build/esp32-poe-iso (env esp32-poe-iso) ---
  http://192.168.1.91:8080/firmware.bin    (1418896 bytes)
  http://192.168.1.91:8080/littlefs.bin    (196608 bytes)
```

Paste either URL into the device's portal (Firmware URL / Filesystem URL) and the device
pulls it. Each request is logged, so you can see the device fetch.

It serves **exactly those two paths**, not the build directory — which also holds the
ELF, the map and every object file. `--url-host` overrides the printed hostname for a
device that reaches you by name; `--bind` narrows the interface.

### Driving the pull from the CLI

```sh
./ebus-esp32 push --pull --url https://releases.example.com/ebus/v1.2 --host 192.168.1.42
```

`--url` is the base the **device** downloads from — it is required, since the server is
separate from this tool. The CLI stages both images, arms them, then polls `GET /api/ota`
and prints the device's progress until it completes or reboots. `--fs-only` sends just
the filesystem; `--pull-wait` bounds the poll (default 300s).

### With curl

```sh
# stage the filesystem without starting it, and keep the URL out of NVS
curl -u admin:changeme -X POST -d '{"url":"http://10.0.0.2:8080/littlefs.bin"}' \
     'http://192.168.1.42/api/filesystem?start=0&persist=0'
# the firmware request arms both — one download session, one reboot
curl -u admin:changeme -X POST -d '{"url":"http://10.0.0.2:8080/firmware.bin"}' \
     'http://192.168.1.42/api/firmware?persist=0'
```

| Query flag | Effect |
|---|---|
| `start=0` | stage the pull without beginning it. The next request without the flag arms everything staged. This is what makes a two-image pull deterministic — otherwise the first download starts immediately and the second request races it |
| `persist=0` | do not write the URL to NVS. A workstation serving one image should not become the device's configured `firmware_url` for the rest of its life |

`ota_loop()` (called from `captive_portal_loop()`) performs the work on the main loop,
always filesystem first, and reboots once when both complete. `GET /api/ota` reports
`idle`, `downloading`, `host_not_found`, `file_not_found`, `http_error`, `write_error`,
`download_incomplete` (the connection closed, or sent nothing for 30 s, before the whole
image arrived), `complete`.

### https and redirects

`https://` URLs work. The device looks for a CA at `/certs/<Host>.ca` — same naming as
the MQTT broker certs, so put it in `data/certs/` and flash the filesystem. **Without a
matching cert the transfer still proceeds, encrypted but unverified**, and the log says
so: anything able to intercept the connection could serve its own image. Verify the
server for anything that matters.

TLS costs roughly 40 KB of heap for the session on top of whatever the MQTT transport
holds; the device logs free heap before the handshake so a failure there is diagnosable.

Redirects are followed for GET (`HTTPC_STRICT_FOLLOW_REDIRECTS`), which release hosting
needs — a GitHub or S3 download URL answers with a 30x to a signed location. A plain-http
URL still cannot follow a redirect *to* https.

`firmware_url` and `fs_url` can also be baked in from `device.yml`, so a device knows
where to look with no request at all.

### Why ArduinoOTA (espota) was removed

espota sends a UDP invitation to `device:3232`, and then **the device opens a TCP
connection back to the host**. That reverse connection does not survive NAT, VPNs, routed
subnets, or a host firewall, and it needs its own shared-password scheme separate from the
`device_passphrase` auth the portal already implements. `setup_ota()`/`loop_ota()` were also
never called by anything — dead code presenting a working-looking API. Removal saved ~5 KB
of flash.

---

## Partition layout

`partitions/partitions_ota.csv` (4 MB flash), selected per-env by
`board_build.partitions`:

```
nvs      data nvs    0x9000   0x5000
otadata  data ota    0xe000   0x2000     <- boot slot selector
app0     app  ota_0  0x10000  0x1E0000   <- 1,966,080 B  running firmware
app1     app  ota_1  0x1F0000 0x1E0000   <- 1,966,080 B  OTA target
spiffs   data spiffs 0x3D0000 0x30000    <-   196,608 B  LittleFS
```

Two consequences worth remembering:

- A firmware image must fit in **1,966,080 bytes** — the percentage PlatformIO prints
  after a build is against this number, not total flash.
- A filesystem image must fit in **196,608 bytes**. `littlefs.bin` is built at exactly the
  partition size.

16 MB boards use `partitions/partitions_ota_n16r8.csv`. A confused `otadata` can boot the
wrong slot after a flash; `ebus-esp32 run --erase` resets it.

---

## `Update.h` reference

The Arduino core's low-level writer, used by both paths above:

```cpp
Update.begin(size, U_FLASH | U_SPIFFS);  // size must be known up front
Update.write(data, len);                 // returns bytes written; != len is an error
Update.end(true);                        // finalize; false leaves the image incomplete
Update.abort();
Update.errorString();                    // human-readable failure
Update.isRunning();
```

`U_FLASH` targets the inactive app slot; `U_SPIFFS` targets the filesystem partition
(the constant is named for SPIFFS but is correct for LittleFS).

---

## Security considerations

What is in place today:

- **Authentication** on every OTA endpoint — HTTP Basic with `device_passphrase`, or an
  `X-Session-Token` from `POST /api/login`. Failed attempts are rate limited and lock out.
- **A time-boxed OTA window** — `POST /api/ota {"enabled":true}` opens it and it closes on
  its own, so the write surface is not permanently exposed.
- **Magic-byte rejection** of non-ESP32 app images before anything is written.

What is *not*, and should be understood before this leaves a trusted LAN:

- **Plain HTTP.** The passphrase travels as base64 Basic auth and the image travels in
  clear. Anyone able to observe the LAN can capture both.
- **Unsigned images.** There is no checksum or signature check — whatever is POSTed with
  a valid passphrase gets written. ESP32 secure boot + signed images
  (`CONFIG_SECURE_SIGNED_ON_UPDATE`) is the real answer if devices leave a controlled
  network.
- **A wrong passphrase still costs a full upload.** HTTP has no way to avoid this without
  `Expect: 100-continue`. Nothing reaches flash, but the transfer completes before the 401.

---

## Testing checklist

- [ ] `POST /api/ota {"enabled":true}` returns `ota_enabled: true`
- [ ] A push with no OTA window open is rejected
- [ ] A push with a wrong passphrase returns 401 **and writes nothing**
- [ ] `?reboot=0` leaves the device up and serving the portal
- [ ] Filesystem then firmware in one session produces exactly one reboot
- [ ] A non-firmware file posted to `/api/ota/firmware` is rejected on the magic byte
- [ ] An image larger than the partition fails at `Update.begin()`, not mid-write
- [ ] Interrupting a push mid-transfer leaves the device bootable (old slot still active)
- [ ] Stack low-water figure is comfortably above zero (see [Stack sizing](#stack-sizing))
- [ ] `ebus-esp32 push --ota --host <ip>` completes and the board comes back with the new build

---

## References

- [ESP-IDF OTA API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [arduino-esp32 OTA web update](https://docs.espressif.com/projects/arduino-esp32/en/latest/ota_web_update.html)
- [PlatformIO partition tables](https://docs.platformio.org/en/latest/platforms/espressif32.html#partition-tables)
