# mDNS Service Advertisement

This document describes the mDNS/DNS-SD services advertised by the homie-esp32-sdk.

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
config:
  - device_name: ESP32 POE ISO Homie    # Used for .local hostname
    device_type: sensor
    manufacturer: NESL
    model: ESP32-POE-ISO
    serial_number:                       # Leave empty to use device_id
    firmware_name: ESP32POEISO Firmware
    firmware_version: 1.0.0
    homie_role: device                   # "device" or "controller"
    mqtt_host: mqtt.example.com
    mqtt_port: 1883
```

## Implementation Notes

- The Homie convention does not specify mDNS requirements; this implementation follows standard DNS-SD practices
- Service types follow RFC 6763 naming conventions
- TXT records are limited to 255 bytes per key-value pair
- The `state` TXT record reflects the device state at mDNS initialization time (typically "init")
- Both Ethernet and WiFi hostnames are synchronized to match the mDNS hostname
