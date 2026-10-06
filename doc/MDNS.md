# mDNS

On arduino-esp32 3.x builds the firmware always claims `<device-id>.local` (see [Hostname](#hostname)). Two further jobs each sit behind their own gate:

- **Discovery** (querier, `mdns.discovery`): browse the LAN for an MQTT broker. Outbound queries only.
- **Advertising** (responder, `mdns.advertise`): advertise the `_ebus._tcp`, `_http._tcp`, `_telnet._tcp` and `_device-info._tcp` services.

Homie device discovery itself runs over MQTT and needs neither.

## Defaults and configuration

| Build | Hostname claim | `mdns.advertise` default | `mdns.discovery` default |
|---|---|---|---|
| arduino-esp32 3.x, Ethernet (default, unsuffixed envs) | always | on | on |
| arduino-esp32 3.x, WiFi-AP (`CONFIG_WIFI_AP`) | always | on | off |
| `-arduino2` (arduino-esp32 2.0.x) | only when a gate is on | off | off |

The defaults are the `#if` above `mdns_discovery` in [`src/platform/config.cpp`](../src/platform/config.cpp).

Override the build default in the `mdns:` section of `device.yml` or `config-overlay.yml`; omit the section to keep it:

```yaml
mdns:
  enabled: true      # sets both gates
  discovery: true    # querier; overrides `enabled`
  advertise: false   # responder; overrides `enabled`
```

`mqtt.mode` decides whether discovery is used to find the broker: `configured-only` (the default) never queries, `discovery-with-fallback` tries mDNS first and then the configured `mqtt.host`, and `discovery-only` ignores the configured host. A discovery mode turns `mdns.discovery` on even when the configuration turns it off.

On 3.x builds the gates do not affect the hostname: `enabled: false` stops discovery and advertising, and `<device-id>.local` is still claimed.

### Responder crash on arduino-esp32 2.0.x

The precompiled ESP-IDF mDNS component in arduino-esp32 2.0.x can crash on certain inbound mDNS packets (backtrace: `_mdns_remove_scheduled_answer` from `mdns_parse_packet`). The crash is in the responder path and cannot be fixed from application code; arduino-esp32 3.x carries the upstream rewrite. This is why `-arduino2` envs default both gates off and claim no hostname unless a gate is on. The Arduino `MDNS.begin()` call also starts the responder stack, so a discovery-only build still initializes it, but registers no services.

## Broker discovery

`setup_mdns()` in [`src/platform/network.cpp`](../src/platform/network.cpp) browses `_secure-mqtt._tcp` first and then `_mqtt._tcp`, up to three attempts 3 seconds apart. The first result with a resolved address is used, by IP address, on the advertised port (8883 or 1883 if the SRV record has none). Until a broker is found, `loop_mdns()` repeats the browse every 30 seconds; it stops once a broker is found, or in `discovery-with-fallback` mode once the configured broker is connected.

mDNS is unauthenticated LAN multicast, and any host can advertise an MQTT service. A discovered broker is verified only when a CA certificate for it is already on the device: `/certs/<Host>.ca` named from the broker's IP address, or `/certs/uploaded-cert.ca` (see [Configuration.md](Configuration.md#broker-ca-certificate-tls)). Otherwise envs built without `-DESP32_POE_ISO` trust whatever root certificate the broker presents (or skip verification if fetching it fails), and `-DESP32_POE_ISO` envs connect in plaintext on port 1883. A broker on any port other than 8883, normally a `_mqtt._tcp` result, gets plain MQTT with no TLS.

## Hostname

The hostname is the device ID as resolved at boot from the `device.id` template (default `${chip_id}`, for example `b0b21c90f570.local`), with `device.id_prefix` applied when set. It is the `Device ID:` line in the boot log. One firmware image therefore gives every board its own hostname. `device.name` is only the display name, in `$description` and the `name` TXT key. The firmware checks whether another host already answers for the name and, if so, tries `<id>-2` through `<id>-99`; on `-arduino2` builds this check runs only when advertising. Ethernet and WiFi send the device ID, without a suffix, as their DHCP hostname.

## Advertised services

All four are registered once by `setup_mdns()` when `mdns.advertise` is on.

### `_ebus._tcp`

Device discovery. The port is the configured `mqtt.port`.

| TXT key | Value |
|---|---|
| `homie` | `5.0` |
| `version` | `1` |
| `id` | Device ID |
| `name` | `device.name` |
| `role` | `device.homie_role` (`device` or `controller`) |
| `type` | `device.device_type`, or `device.type` when that is empty; omitted when both are empty |
| `base` | The device's base topic, for example `ebus/5/<device-id>/` (`homie/5/` without `-DUSE_EBUS_TOPIC`) |
| `state` | `init` (the state at registration; not updated afterward) |
| `implementation` | `homie-esp32-sdk` |
| `ca_cert_path` | `/api/broker-cert`, the REST path for the broker CA certificate |

### `_http._tcp`

The web UI and REST API on port 80.

| TXT key | Value |
|---|---|
| `path` | `/api/openapi.yml` |
| `api` | `openapi` |

### `_telnet._tcp`

The read-only serial log stream on `log.tcp_port` (default 23). Registered only when the listener is running, so `log.tcp_port: 0` omits it.

| TXT key | Value |
|---|---|
| `kind` | `serial-log` |

### `_device-info._tcp`

Hardware and firmware details, on port 80.

| TXT key | Value |
|---|---|
| `manufacturer` | `device.manufacturer`; omitted when empty |
| `model` | `device.model` |
| `serial_number` | `device.serial_number`, or the device ID when that is empty |
| `eth_mac` | Ethernet MAC address |
| `wlan_mac` | WiFi MAC address |
| `fw_name` | `device.firmware_name`; omitted when empty |
| `fw_version` | `device.firmware_version` |
| `platform` | `ESP32` |
| `sdk_version` | ESP-IDF version (`ESP.getSdkVersion()`) |
| `eth_ip` | Ethernet IP; omitted when Ethernet has no address |
| `wlan_ip` | WiFi station IP; omitted when WiFi has no address |

## Browsing from a host

```bash
# Linux
avahi-browse -rt _ebus._tcp
avahi-browse -rt _telnet._tcp

# macOS
dns-sd -G v4 <device-id>.local
dns-sd -B _ebus._tcp
dns-sd -L "<instance-name>" _ebus._tcp local
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
