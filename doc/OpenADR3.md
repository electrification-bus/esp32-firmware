# OpenADR 3 VEN

A top-level `oa3:` block in `device.yml` builds the [cpp-oa3](https://github.com/grid-coordination/cpp-oa3) OpenADR 3 VEN client into the firmware, in any role. The VEN finds its program on the VTN, polls the program's events, and logs the price in force to the serial log, which the network console also carries. It runs on a FreeRTOS task of its own (`src/platform/oa3_port.cpp`), so it polls whether or not the board has an eBus broker.

## Example

A board that only follows the price server, with no broker:

```yaml
device:
  name: price-follower
  type: price-follower
  environment: esp32-poe-iso
  role: standalone
  device_passphrase: ""  # set it in config-overlay.yml

oa3:
  base_url: https://price.grid-coordination.energy/openadr3/3.1.0
  program_name: EELEC-012041131
```

The VTN's CA certificate goes in `data/certs/priceGrid-coordinationEnergy.ca` (see [CA certificate](#ca-certificate)).

## Keys

| Key | Required | Description |
|---|---|---|
| `base_url` | yes | The VTN's URL up to the spec version segment, `https://` or `http://`, at most 255 characters |
| `program_name` | one of the two | The program's name, resolved through the VTN's `/programs`, at most 63 characters |
| `program_id` | one of the two | The program's id, fetched as `/programs/<id>`, at most 63 characters; with `program_name` as well, the name must match |
| `poll_period_s` | no | Seconds between polls, 10 to 715827; the client's default is 900 |
| `max_events` | no | Events the client holds, 1 to 64, default 2 |

`generate` validates the block (`oa3_config()` in `ebus-esp32`) and refuses an unknown key, a value of the wrong type, or one out of range. It copies `base_url`, `program_name`, `program_id` and `poll_period_s` into the `oa3:` section of `.ebus/data/config.yml`, read at boot. `max_events` sizes the client's event arrays, so it becomes the build flag `-DOA3_MAX_EVENTS`, along with `-DEBUS_OA3`.

## CA certificate

With an `https://` `base_url`, the VEN verifies the VTN against a CA certificate at `/certs/<Host>.ca`, named by `mqtt_build_cert_path()` in `src/platform/config.cpp` as broker and OTA certificates are: the host with its dots removed and the letter after each dot capitalized. Put the PEM root in `data/certs/` (the repo's or the project's) and flash the filesystem image (`ebus-esp32 push --fs-only`). The file must be smaller than 4096 bytes (`OA3_CA_MAX`).

Without the file the VEN does not start and logs `OA3: **ERROR -- no CA certificate for <host> at <path>`. An `http://` `base_url` needs no certificate.

## Role: standalone

`device.role: standalone` builds a board with no broker and no Homie device. It keeps the network, the web server and OTA, the network log, the mDNS hostname, NTP and recovery mode, and skips MQTT altogether; `mqtt.mode` is ignored. `generate` refuses `nodes:`, `children:` or `links:` in a standalone build, and it notes a standalone build without `oa3:`, which runs only the network, web server and OTA. A standalone build advertises no `_ebus._tcp` service ([MDNS.md](MDNS.md)).

In a `device` or `controller` build, the VEN starts at the end of `setup()`, after the role's own bring-up.

## Library version

The build uses cpp-oa3 pinned in `ebus-esp32` (`OA3_LIB_DEP`):

```
oa3=https://github.com/grid-coordination/cpp-oa3.git#v0.1.0
```

`EBUS_CPP_OA3` names another source for a build, for example a local checkout:

```sh
EBUS_CPP_OA3=symlink:///path/to/cpp-oa3 ./ebus-esp32 build <project>
```

`generate` prints the source it used on its `oa3:` summary line.

## Log

Every line starts with `OA3:`.

| Line | When |
|---|---|
| `VEN for <program> at <base_url>, up to <n> events` | the client started |
| `CA certificate <path>` | the CA file was read |
| `price <value> <currency>/<units>, interval <id> of event <id>, <start> to <end>` | the first price evaluation, and each change of the price in force |
| `no price in force` | as above, when no interval covers the current time |
| `status <status> (http <result>, <code>; <n> failures; retry in <s> s)` | the client reports its status |
| `poll <status>, <n> events; internal free ..., largest ..., min ...; task stack free ...; arena high water ... of ...` | after each request: internal heap, the task's stack high-water mark and the parse arena's use |
| `GET <host> failed: <error>` | the request failed before a response; a handshake or certificate failure adds `, TLS: <error>` |
| `<level> <message>` | the client library's own log, at `info` and above |
| `**ERROR -- ...` | the VEN did not start: no `base_url`, no CA file, no PSRAM for its storage, or a configuration the client rejects |

The client reads UTC from the clock NTP sets; until NTP has set it, the client's status is `WAITING_FOR_CLOCK` and it makes no request.

## Memory

With arduino-esp32 3.3.11 (ESP-IDF 5.5.5), Mbed TLS allocates only from internal RAM and uses 16 KB records, and one TLS session takes 42 to 45 KB of internal heap. On an ESP32-POE-ISO-WROVER, an HTTPS request made while an MQTTS session was open succeeded, but the internal heap's low-water mark fell to 232 bytes. An ESP32-P4 kept 160 KB.

The VEN opens its connection for each request and closes it before the request returns, so it holds no TLS session between polls. A `device` or `controller` build with a TLS broker still holds two sessions during a poll, so `generate` warns when `oa3:` and a TLS broker (`mqtt.port: 8883`, or `secure-mqtt` in `mqtt.discover`) share a build for an original-ESP32 environment.

On a `BOARD_HAS_PSRAM` build, the client's event arrays and parse arena are allocated in PSRAM. Otherwise they are static in internal RAM, about 38 KB with the default `max_events: 2`, and `generate` warns when `max_events` is larger. The VEN's task has an 8 KB stack (`OA3_TASK_STACK`).
