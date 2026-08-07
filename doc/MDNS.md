# mDNS

This document covers how the firmware uses mDNS/DNS-SD: the use cases it serves, the two distinct mechanisms involved, and the services it advertises.

> **Default: build-conditional.** Both gates default **ON** for Ethernet-only builds on arduino-esp32 3.x (`esp32-poe-iso*-idf5`) and **OFF** everywhere else — WiFi-AP builds and every 2.0.x env. See [`config.cpp`](../src/platform/config.cpp#L41) for the test and `device.yml` for the reasoning. The bundled ESP-IDF mDNS **responder** can crash on certain incoming packets (a null-deref in the precompiled `libmdns.a` on 2.0.x, triggered by inbound mDNS traffic, not by our code); 3.x carries the upstream rewrite. Homie device discovery runs over MQTT and does not need mDNS, so builds that have a captive portal to fall back on leave it off. Override per-device with an `mdns:` section in `device.yml` (or `config-overlay.yml`) — omit the section entirely to keep the build default. See the *Crash status* note below.

## Use cases & two mechanisms

mDNS does two unrelated jobs here, and the distinction matters because the crash lives in only one of them:

- **Querier (client)** — the ESP32 *browses* for services and *resolves* `.local` names. Outbound queries; the safe side.
- **Responder (server)** — the ESP32 *claims* a `.local` hostname and *answers* queries about itself and the services it advertises. The inbound-packet path where the crash occurs.

Three use cases, in priority order:

| # | Use case | Mechanism | Priority |
|---|---|---|---|
| **UC2** | **Discover the eBus MQTT broker, then auth + connect.** Browse `_secure-mqtt._tcp` / `_mqtt._tcp`, get host/port, connect. Also lets the ESP32 reach the broker and REST API by `.local` name. | **querier** | **highest** |
| **UC3** | **Claim & defend the device's own `<name>.local`** so other hosts can reach the ESP32 (web UI / REST / OTA) by name. | responder (hostname + `_http`) | medium |
| **UC1** | **Advertise device metadata** (`_device-info`, `_ebus`/`_homie` TXT records). | responder (services + TXT) | least |

Two consequences fall out of this:

1. **UC2 is the high-value, crash-safe path; UC1 is the low-value, crash-prone one.** So the cheap, valuable capability (find the broker) does not require the risky responder at all.
2. **Discovery is transient.** Once the ESP32 has found and connected to the broker, it does not need to keep listening — UC2's browse is a one-shot at boot (a few attempts, then stop). The querier only re-engages as a reconnect fallback (below).

### The split gate (implemented, ESP32-6du.6)

A single flag used to gate *both* mechanisms, which left UC2 hostage to the crash-prone responder. There are now two independent gates, so discovery can run with the responder off:

- `mdns.discovery` — the querier (UC2): browse for the broker, resolve `.local` names.
- `mdns.advertise` — the responder (UC1/UC3): claim the hostname, register services.

[`setup_mdns()`](../src/platform/network.cpp#L203) honours them separately: it returns early only when *both* are off, registers services solely under `mdns_advertise` ([network.cpp:262](../src/platform/network.cpp#L262)), and runs the broker query solely under `mdns_discovery` ([network.cpp:257](../src/platform/network.cpp#L257)). The reconnect re-query is likewise the querier's job ([network.cpp:514](../src/platform/network.cpp#L514)).

`mdns.enabled` remains as a legacy convenience that sets both at once; the granular keys override it. Note the Arduino `MDNS.begin()` API couples `mdns_init()` with a hostname claim, so a discovery-only build still initialises the responder stack even though it registers no services.

## UC2 detail: broker discovery and (re)connection

The querier browses for the broker service and obtains its address. **Connection policy: connect by the discovered IP**, which keeps the querier idle after boot. The broker's `.local` hostname is **persisted as a fallback**: if a later reconnect-by-IP fails (e.g. the broker's DHCP lease moved it), the firmware re-resolves the cached `.local` name. Discovery stays transient; only the resolver remains available.

Broker certs are expected to carry SANs for **both** the `.local` hostname and the IP address(es), so either connection form validates cleanly under TLS — connecting by IP is *not* a TLS-correctness compromise. (This is an eBus-ecosystem expectation, captured as guidance in the specification repo.)

The `MqttMode` config selects how the broker is located: `DISCOVERY_ONLY`, `DISCOVERY_WITH_FALLBACK` (mDNS first, then configured `mqtt_host`), or `CONFIGURED_ONLY` (no mDNS).

### Trust: mDNS gives you an address, not an identity

mDNS is unauthenticated LAN multicast — a rogue host can advertise a fake `_mqtt._tcp`. Discovery only bootstraps *where* the broker is; *who* it is comes entirely from TLS certificate/CA validation (and, where configured, mTLS). Treat a discovered broker as untrusted until the TLS handshake validates it.

## Crash status

The responder crash is an upstream bug in the precompiled ESP-IDF mDNS component shipped with arduino-esp32 2.0.x (backtrace: `_mdns_remove_scheduled_answer` ← `mdns_parse_packet`). It is not fixable with an app-level change. Mitigation today is the default-OFF gate. The gate split (above) makes UC2 usable without touching the crashy path; arduino-esp32 3.x (where mDNS is rewritten upstream) is the longer-term fix. All tracked in beads.

---

The remainder of this document describes the **responder** side — the hostname claim and the services advertised (UC1/UC3).

## Hostname

The device advertises as `{device-name}.local` where `{device-name}` is derived from `device_name` in `config.yml`, sanitized for mDNS compatibility:

- Uppercase converted to lowercase
- Spaces and underscores converted to hyphens
- Non-alphanumeric characters removed

Falls back to `{device_id}.local` if no device name is configured.

## Services

### 1. `_homie._tcp` - Homie Protocol Discovery

Primary service for Homie-compliant device/controller discovery. Port: MQTT port (default 1883).

| TXT Record | Example Value | Description |
|------------|---------------|-------------|
| `homie` | `5.0` | Homie convention version (without patch) |
| `version` | `1` | Description document version |
| `id` | `NESL123456` | Unique device ID (MAC-based) |
| `name` | `ESP32 POE ISO Homie` | Friendly device name |
| `role` | `device` or `controller` | Homie role |
| `type` | `sensor` | Device type |
| `base` | `homie/5/NESL123456/` | MQTT base topic for this device |
| `mqtt_host` | `mqtt.example.com` | MQTT broker hostname |
| `mqtt_port` | `1883` | MQTT broker port |
| `mqtt_tls` | `true` or `false` | TLS/SSL enabled |
| `state` | `init`, `ready`, etc. | Current device state |
| `implementation` | `homie-esp32-sdk` | Implementation identifier |
| `ca_cert_path` | `/api/broker-cert` | REST path to download/upload broker CA cert |

### 2. `_mqtt._tcp` - MQTT Broker Discovery

Standard DNS-SD service type for MQTT broker discovery (RFC 6763). Port: MQTT port.

| TXT Record | Example Value | Description |
|------------|---------------|-------------|
| `host` | `mqtt.example.com` | MQTT broker hostname |
| `port` | `1883` | MQTT broker port |
| `tls` | `true` or `false` | TLS/SSL enabled |
| `auth` | `true` or `false` | Authentication required |

### 3. `_device-info._tcp` - Hardware/Firmware Information

Device information service for management and discovery tools. Port: 80.

| TXT Record | Example Value | Description |
|------------|---------------|-------------|
| `manufacturer` | `NESL` | Device manufacturer |
| `model` | `ESP32-POE-ISO` | Device model |
| `serial_number` | `NESL123456` | Serial number (or device ID) |
| `eth_mac` | `AA:BB:CC:DD:EE:FF` | Ethernet MAC address |
| `wlan_mac` | `AA:BB:CC:DD:EE:00` | WiFi MAC address |
| `eth_ip` | `192.168.1.100` | Ethernet IP (if connected) |
| `wlan_ip` | `192.168.1.101` | WiFi IP (if connected) |
| `fw_name` | `ESP32POEISO Firmware` | Firmware name |
| `fw_version` | `1.0.0` | Firmware version |
| `platform` | `ESP32` | Hardware platform |
| `sdk_version` | `v4.4.x` | ESP-IDF SDK version |

## Discovery Examples

### Using avahi-browse (Linux)

```bash
# Discover all Homie devices
avahi-browse -rt _homie._tcp

# Discover MQTT brokers
avahi-browse -rt _mqtt._tcp

# Discover device info
avahi-browse -rt _device-info._tcp
```

### Using dns-sd (macOS)

```bash
# Discover all Homie devices
dns-sd -B _homie._tcp

# Get TXT records for a specific service
dns-sd -L "esp32-poe-iso-homie" _homie._tcp local
```

### Using Python (zeroconf)

```python
from zeroconf import ServiceBrowser, Zeroconf

class MyListener:
    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        print(f"Service {name}: {info.properties}")

zeroconf = Zeroconf()
browser = ServiceBrowser(zeroconf, "_homie._tcp.local.", MyListener())
```

## Configuration

mDNS advertisement is configured via `data/config.yml`:

```yaml
device:
  name: ESP32 POE ISO Homie        # Used for .local hostname
  device_type: sensor
  manufacturer: NESL
  model: ESP32-POE-ISO
  serial_number:                   # Leave empty to use device id
  firmware_name: ESP32POEISO Firmware
  firmware_version: 1.0.0
  homie_role: device               # "device" or "controller"
mqtt:
  host: mqtt.example.com
  port: 1883
```

## Implementation Notes

- The Homie convention does not specify mDNS requirements; this implementation follows standard DNS-SD practices
- Service types follow RFC 6763 naming conventions
- TXT records are limited to 255 bytes per key-value pair
- The `state` TXT record reflects the device state at mDNS initialization time (typically "init")
- Both Ethernet and WiFi hostnames are synchronized to match the mDNS hostname
