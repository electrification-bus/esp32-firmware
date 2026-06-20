# OTA Firmware Server

A simple Python HTTP server for serving firmware binaries to ESP32 devices during OTA updates.

## Quick Start

From the project root directory:

```bash
cd .pio/build/esp32-poe-iso && python -m http.server 8080 --bind 0.0.0.0
```

This serves firmware files from the PlatformIO build output directory.

## Binding Options

### All interfaces (for ESP32 access)
```bash
python -m http.server 8080 --bind 0.0.0.0
```
Use this when ESP32 devices need to download firmware over the network.

### Localhost only (local testing)
```bash
python -m http.server 8080 --bind localhost
```
Use this for local testing only. ESP32 devices cannot reach localhost.

## Triggering OTA via MQTT

From MQTT Explorer or any MQTT client, publish to:

**Topic:**
```
homie/5/{device-id}/ota/firmware-url/set
```

**Payload:**
```
http://{your-pc-ip}:8080/firmware.bin
```

Replace `{device-id}` with your device's Homie ID and `{your-pc-ip}` with your PC's local IP address (e.g., `192.168.1.100`).

To find your PC's IP address:
- Windows: `ipconfig | findstr IPv4`
- Linux/Mac: `ip addr` or `ifconfig`

## Firmware Location

After building with PlatformIO, firmware binaries are located at:
```
.pio/build/{environment}/firmware.bin
```

For example:
- `.pio/build/esp32-poe-iso/firmware.bin`

## Partition Tables

OTA requires a dual-partition scheme. Two partition tables are available:

### partitions_ota.csv (4MB flash - default)

| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 20KB | Non-volatile storage |
| otadata | 8KB | OTA boot selection |
| app0 | ~1.9MB | Running firmware (ota_0) |
| app1 | ~1.9MB | OTA target (ota_1) |
| spiffs | 192KB | File system |

### partitions_ota_n16r8.csv (16MB flash)

For ESP32 N16R8 variants with 16MB flash and 8MB PSRAM:

| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 20KB | Non-volatile storage |
| otadata | 8KB | OTA boot selection |
| app0 | ~4MB | Running firmware (ota_0) |
| app1 | ~4MB | OTA target (ota_1) |
| spiffs | ~8MB | File system |

To use the N16R8 partition table, update `platformio.ini`:
```ini
board_build.partitions = partitions_ota_n16r8.csv
```

**Note:** The first flash after changing partition tables must be done via USB/serial, not OTA.

## Troubleshooting

### HTTP error -1
Connection failed. Common causes:
- Server bound to `localhost` instead of `0.0.0.0`
- Firewall blocking port 8080
- Wrong IP address in the URL
- Server not running

### URL truncation
The firmware URL must fit within 256 characters. If you see partial URLs in logs, check that `Property._value` buffer size is sufficient in `include/homie/Property.h`.

## Stopping the Server

- Press `Ctrl+C` in the terminal running the server
- Or find and kill the process:
  ```bash
  # Windows
  netstat -ano | findstr :8080
  taskkill //F //PID {pid}

  # Linux/Mac
  lsof -i :8080
  kill {pid}
  ```
