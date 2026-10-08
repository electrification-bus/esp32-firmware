# mDNS

On arduino-esp32 3.x builds the firmware claims `<device-id>.local` (see [Hostname](#hostname)) unless mDNS is turned off entirely. Two further jobs each sit behind their own gate:

- **Discovery** (querier, `mdns.discovery`): browse the LAN for an MQTT broker. Outbound queries only.
- **Advertising** (responder, `mdns.advertise`): advertise the `_ebus._tcp`, `_http._tcp`, `_telnet._tcp` and `_device-info._tcp` services.

Homie device discovery itself runs over MQTT and needs neither.

## Defaults and configuration

| Build | Hostname claim | `mdns.advertise` default | `mdns.discovery` default |
|---|---|---|---|
| arduino-esp32 3.x, Ethernet (default, unsuffixed envs) | yes | on | on |
| arduino-esp32 3.x, WiFi-AP (`CONFIG_WIFI_AP`) | yes | on | off |
| `-arduino2` (arduino-esp32 2.0.x) | only when a gate is on | off | off |

The defaults are the `#if` above `mdns_discovery` in [`src/platform/config.cpp`](../src/platform/config.cpp).

Override the build default in the `mdns:` section of `device.yml` or `config-overlay.yml`; omit the section to keep it:

```yaml
mdns:
  advertise: false   # responder; keeps <device-id>.local, drops the service records
  discovery: true    # querier
```

To save heap, `advertise: false` is the lighter option: on 3.x the device keeps answering at `<device-id>.local` and stops advertising its services. Note that the eBus specification requires `_ebus._tcp` and `_device-info._tcp` (see [Advertised services](#advertised-services)).

`enabled: false` is the full opt-out: no mDNS at all on every build. The firmware does not call `MDNS.begin()`, claims no hostname, registers no services and runs no querier, and it overrides `discovery` and `advertise`. The device is then reachable only by IP address. `generate` prints a warning when the merged configuration sets it, and the device logs `mDNS: disabled by config; not claiming <device-id>.local` at boot. `enabled: true` turns both gates on. The DHCP hostname is independent of mDNS and is sent either way.

| Configuration | 3.x hostname | 3.x advertise | 3.x discovery | `-arduino2` |
|---|---|---|---|---|
| no `mdns:` section | yes | on | on (Ethernet), off (WiFi-AP) | nothing starts |
| `advertise: false` | yes | off | as the default | nothing starts |
| `enabled: false` | no | off | off | nothing starts |
| `enabled: false` with a discovery `mqtt.mode` | yes | off | on | querier and hostname |

`mqtt.mode` decides whether discovery is used to find the broker: `configured-only` (the default) never queries, `discovery-with-fallback` tries mDNS first and then the configured `mqtt.host`, and `discovery-only` ignores the configured host. A discovery mode turns `mdns.discovery` on even when the configuration turns it off, and it wins over `enabled: false`, since the device could not find its broker otherwise: the querier starts, which through the Arduino `MDNS.begin()` call also claims the hostname, but no services are advertised. Both `generate` and the boot log (`CONFIG: mqtt_mode needs discovery, which overrides mdns.enabled=false`) say so.

### Responder crash on arduino-esp32 2.0.x

The precompiled ESP-IDF mDNS component in arduino-esp32 2.0.x can crash on certain inbound mDNS packets (backtrace: `_mdns_remove_scheduled_answer` from `mdns_parse_packet`). The crash is in the responder path and cannot be fixed from application code; arduino-esp32 3.x carries the upstream rewrite. This is why `-arduino2` envs default both gates off and claim no hostname unless a gate is on. The Arduino `MDNS.begin()` call also starts the responder stack, so a discovery-only build still initializes it, but registers no services.

## Broker discovery

`setup_mdns()` in [`src/platform/network.cpp`](../src/platform/network.cpp) browses the service types in `mqtt.discover`, in order, up to three attempts 3 seconds apart. The default is `_secure-mqtt._tcp` and then `_mqtt._tcp`. The first result with a resolved address is used, by IP address, on the advertised port (the service's standard port, 8883 or 1883, if the SRV record has none). Until a broker is found, `loop_mdns()` repeats the browse every 30 seconds; it stops once a broker is found, or in `discovery-with-fallback` mode once the configured broker is connected.

```yaml
mqtt:
  mode: discovery-only
  discover: [secure-mqtt]   # TLS-only: never pick a plain _mqtt._tcp broker
```

| `mqtt.discover` value | Service browsed | Supported |
|---|---|---|
| `secure-mqtt` | `_secure-mqtt._tcp` (TLS, 8883) | yes |
| `mqtt` | `_mqtt._tcp` (plain, 1883) | yes |
| `mqtt-ws` | `_mqtt-ws._tcp` (WebSocket, 9001) | no |
| `mqtt-wss` | `_mqtt-wss._tcp` (secure WebSocket, 9002) | no |

The eBus specification's preference order is `_secure-mqtt._tcp`, `_mqtt-wss._tcp`, `_mqtt-ws._tcp`, `_mqtt._tcp`. The firmware's MQTT client connects over TCP or TLS only, so `generate` refuses the two WebSocket values, as well as an empty list, a duplicate or an unknown name. A hand-written `data/config.yml` with a bad entry has it skipped at boot, and the default is used if nothing usable is left.

The broker's `.local` host name is kept alongside its address. When three consecutive reconnects to the address fail (the reconnect interval is 10 seconds), the firmware sends one mDNS query for the host name. If the broker answers from a new address, the firmware switches to it, re-selects the transport and CA certificate for it, and reconnects at once; otherwise it keeps the old address and asks again after three more failures. This covers a broker whose DHCP lease changed. It is a single host query, not a new browse. A configured broker (`mqtt.host`) is never re-resolved.

mDNS is unauthenticated LAN multicast, and any host can advertise an MQTT service. A discovered broker is verified only when a CA certificate for it is already on the device: `/certs/<Host>.ca` named from the broker's IP address, or `/certs/uploaded-cert.ca` (see [Configuration.md](Configuration.md#broker-ca-certificate-tls)). Otherwise envs built without `-DESP32_POE_ISO` trust whatever root certificate the broker presents (or skip verification if fetching it fails), and `-DESP32_POE_ISO` envs connect in plaintext on port 1883. A broker on any port other than 8883, normally a `_mqtt._tcp` result, gets plain MQTT with no TLS.

## Hostname

The hostname is the device ID as resolved at boot from the `device.id` template (default `${chip_id}`, for example `b0b21c90f570.local`), with `device.id_prefix` applied when set. It is the `Device ID:` line in the boot log. One firmware image therefore gives every board its own hostname. `device.name` is only the display name, in `$description` and the `name` TXT key. The firmware checks whether another host already answers for the name and, if so, tries `<id>-2` through `<id>-99`; on `-arduino2` builds this check runs only when advertising. Ethernet and WiFi send the device ID, without a suffix, as their DHCP hostname.

## Advertised services

All four are registered once by `setup_mdns()` when `mdns.advertise` is on. Each service's instance name is the hostname. The TXT keys follow "Detail: mDNS Discovery" in the eBus specification, [framework.md](https://github.com/electrification-bus/specification/blob/main/framework.md) version 0.9 (`EBUS_SPEC_VERSION` in [`lib/ebus_core/include/platform/mdns_strings.h`](../lib/ebus_core/include/platform/mdns_strings.h)). Requirement 6 makes `_ebus._tcp` and `_device-info._tcp` mandatory, and requirement 8 makes `_http._tcp` mandatory for an entity with a REST API. The Spec column gives each key's level in that section's tables; "compatibility" marks a key for compatibility with existing eBus devices (SPAN panels), not in framework.md 0.9. A recommended key whose value is empty is omitted.

### `_ebus._tcp`

Entity discovery (requirement 6). The port is the configured `mqtt.port`.

| TXT key | Spec | Value |
|---|---|---|
| `txtvers` | required | `1` |
| `ebus_version` | required | `0.9` |
| `roles` | required | `controller` when `device.homie_role` is `controller`, otherwise `device` |
| `device_id` | required | Device ID |
| `device_type` | recommended | `device.device_type`, or `device.type` when that is empty |
| `name` | recommended | `device.name` |
| `manufacturer` | recommended | `device.manufacturer`, or `unknown` when that is empty |
| `model` | recommended | `device.model`, or `ESP32` when that is empty |
| `fw_version` | recommended | `device.firmware_version` |
| `register` | recommended | `/api/v1/auth/register`, which exchanges the device passphrase for an access token (see [Authentication.md](Authentication.md)) |
| `auth_methods` | recommended | `passphrase,preconfigured`: `passphrase` for the `register` endpoint, `preconfigured` because the passphrase itself (`device.device_passphrase`) is set out of band, in the configuration or on the setup page |
| `homie_domain` | compatibility | The Homie topic domain: `ebus` with `-DUSE_EBUS_TOPIC`, otherwise `homie` |
| `homie_version` | compatibility | `5` |
| `homie_roles` | compatibility | Same value as `roles` |

### `_device-info._tcp`

Hardware and firmware details (requirement 6), on port 80.

| TXT key | Spec | Value |
|---|---|---|
| `txtvers` | required | `1` |
| `manufacturer` | required | `device.manufacturer`, or `unknown` when that is empty |
| `model` | required | `device.model`, or `ESP32` when that is empty |
| `serial_number` | required | `device.serial_number`, or the device ID when that is empty |
| `fw_version` | recommended | `device.firmware_version` |
| `os_version` | recommended | Framework versions from their build macros, for example `arduino-esp32 3.3.11 / ESP-IDF 5.5.5` |
| `mac` | recommended | Primary MAC in lowercase hex without colons: the Ethernet MAC on Ethernet builds, the WiFi MAC on `CONFIG_WIFI_AP` builds |

### `_http._tcp`

The web UI and REST API (requirement 8), on port 80.

| TXT key | Spec | Value |
|---|---|---|
| `txtvers` | required | `1` |
| `path` | required | `/api/v1` |
| `version` | required | `1.3.0`, the `info.version` of [`data/openapi.yml`](../data/openapi.yml) |
| `device_id` | required | Device ID |
| `device_type` | required | `device.device_type`, or `device.type` when that is empty; omitted when both are empty |
| `openapi` | recommended | `/api/v1/openapi.yml` (also served at `/api/openapi.yml`) |

### `_telnet._tcp`

The read-only serial log stream on `log.tcp_port` (default 23). Registered only when the listener is running, so `log.tcp_port: 0` omits it. framework.md does not define this service; it is proposed upstream as [electrification-bus/specification issue 25](https://github.com/electrification-bus/specification/issues/25).

| TXT key | Value |
|---|---|
| `txtvers` | `1` |
| `device_id` | Device ID |
| `kind` | `serial-log` |

## Browsing from a host

```bash
# Linux
avahi-browse -rt _ebus._tcp
avahi-browse -rt _telnet._tcp

# macOS
dns-sd -G v4 <device-id>.local
dns-sd -B _ebus._tcp
dns-sd -L <device-id> _ebus._tcp local
```

```python
from zeroconf import ServiceBrowser, Zeroconf

class Listener:
    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        print(name, info.properties)

    def update_service(self, zc, type_, name):
        pass

    def remove_service(self, zc, type_, name):
        pass

zc = Zeroconf()
browser = ServiceBrowser(zc, "_ebus._tcp.local.", Listener())
```
