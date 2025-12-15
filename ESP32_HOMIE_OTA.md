# ESP32 Homie OTA (Over-The-Air) Update Implementation

This document outlines the concepts, architecture, and implementation steps for enabling OTA firmware updates on ESP32 devices using the Homie MQTT specification.

## Table of Contents

1. [Overview](#overview)
2. [ESP32 OTA Capabilities](#esp32-ota-capabilities)
3. [Homie OTA Node Specification](#homie-ota-node-specification)
4. [OTA Update Flow](#ota-update-flow)
5. [Implementation Architecture](#implementation-architecture)
6. [Security Considerations](#security-considerations)
7. [Implementation Steps](#implementation-steps)
8. [Example Code Structure](#example-code-structure)

---

## Overview

OTA (Over-The-Air) updates allow firmware to be uploaded to ESP32 devices remotely without physical access. By exposing OTA capabilities through the Homie MQTT specification, we enable:

- Remote firmware upgrades via MQTT commands
- Status monitoring during updates
- Version tracking and rollback capabilities
- Integration with home automation controllers

### Key Components

| Component | Description |
|-----------|-------------|
| **ESP32 Update Library** | Arduino framework's built-in OTA support (`Update.h`) |
| **Homie OTA Node** | MQTT node exposing OTA properties and commands |
| **Firmware Server** | HTTP(S) server hosting firmware binaries |
| **Controller** | Homie controller initiating and monitoring updates |

---

## ESP32 OTA Capabilities

### Arduino Framework OTA Libraries

The ESP32 Arduino framework provides several OTA mechanisms:

#### 1. `Update.h` - Core Update Library

The low-level library for writing firmware to flash:

```cpp
#include <Update.h>

// Key functions:
Update.begin(size_t size);           // Initialize update with expected size
Update.write(uint8_t *data, size_t len);  // Write chunk of firmware
Update.end(bool evenIfRemaining);    // Finalize update
Update.hasError();                   // Check for errors
Update.getError();                   // Get error code
Update.errorString();                // Get error description
Update.progress();                   // Bytes written so far
Update.size();                       // Total size to write
Update.isRunning();                  // Update in progress
Update.abort();                      // Cancel update
```

#### 2. `HTTPUpdate.h` - HTTP-based OTA

Simplified HTTP download and update:

```cpp
#include <HTTPUpdate.h>

// Pull firmware from URL
t_httpUpdate_return ret = httpUpdate.update(client, "http://server/firmware.bin");
// Returns: HTTP_UPDATE_FAILED, HTTP_UPDATE_NO_UPDATES, HTTP_UPDATE_OK
```

#### 3. `ArduinoOTA.h` - IDE/Network OTA

For development/IDE integration (less suitable for production Homie use):

```cpp
#include <ArduinoOTA.h>

ArduinoOTA.setHostname("esp32-device");
ArduinoOTA.begin();
ArduinoOTA.handle();  // Call in loop()
```

### ESP32 Partition Scheme

OTA requires a dual-partition scheme:

```
┌─────────────────────────────┐
│     Bootloader (0x1000)     │
├─────────────────────────────┤
│   Partition Table (0x8000)  │
├─────────────────────────────┤
│      NVS (Non-Volatile)     │
├─────────────────────────────┤
│    OTA Data (boot select)   │
├─────────────────────────────┤
│      APP0 (ota_0)           │  ← Running firmware
├─────────────────────────────┤
│      APP1 (ota_1)           │  ← OTA target partition
├─────────────────────────────┤
│      SPIFFS/LittleFS        │
└─────────────────────────────┘
```

**PlatformIO Configuration** (already in project):
```ini
board_build.partitions = huge_app.csv
```

For OTA support, use a partition scheme with two app partitions:
```ini
board_build.partitions = min_spiffs.csv   ; or default.csv
```

---

## Homie OTA Node Specification

Following the Homie 5.0 convention and this SDK's patterns, the OTA node exposes:

### Node Definition

```
homie/5/{device-id}/$description → {
  "nodes": {
    "ota": {
      "name": "OTA Update",
      "type": "ota",
      "properties": {
        "enabled": { ... },
        "state": { ... },
        "progress": { ... },
        "version": { ... },
        "firmware-url": { ... },
        "checksum": { ... },
        "message": { ... }
      }
    }
  }
}
```

### Properties

| Property | Datatype | Settable | Retained | Description |
|----------|----------|----------|----------|-------------|
| `enabled` | boolean | yes | yes | Enable/disable OTA acceptance |
| `state` | enum | no | yes | Current OTA state |
| `progress` | integer | no | yes | Update progress (0-100%) |
| `version` | string | no | yes | Current firmware version |
| `firmware-url` | string | yes | no | URL to download firmware from |
| `checksum` | string | yes | no | MD5/SHA256 checksum for verification |
| `message` | string | no | yes | Status/error message |

### State Enum Values

| State | Description |
|-------|-------------|
| `idle` | Ready for OTA, no update in progress |
| `downloading` | Downloading firmware from URL |
| `verifying` | Verifying checksum |
| `updating` | Writing firmware to flash |
| `rebooting` | Update complete, rebooting |
| `error` | Error occurred (see message property) |

### MQTT Topics

```
# Device publishes:
homie/5/{device-id}/ota/enabled      → "true"
homie/5/{device-id}/ota/state        → "idle"
homie/5/{device-id}/ota/progress     → "0"
homie/5/{device-id}/ota/version      → "1.2.3"
homie/5/{device-id}/ota/message      → ""

# Controller sends commands:
homie/5/{device-id}/ota/enabled/set      → "true" | "false"
homie/5/{device-id}/ota/firmware-url/set → "http://server/firmware.bin"
homie/5/{device-id}/ota/checksum/set     → "abc123..." (optional)
```

---

## OTA Update Flow

### Sequence Diagram

```
Controller                    ESP32 Device                  Firmware Server
    │                              │                              │
    │  1. Set firmware-url         │                              │
    ├─────────────────────────────>│                              │
    │                              │                              │
    │  2. Publish state=downloading│                              │
    │<─────────────────────────────│                              │
    │                              │                              │
    │                              │  3. HTTP GET firmware.bin    │
    │                              ├─────────────────────────────>│
    │                              │                              │
    │                              │  4. Firmware binary stream   │
    │                              │<─────────────────────────────│
    │                              │                              │
    │  5. Publish progress=N       │                              │
    │<─────────────────────────────│  (repeated)                  │
    │                              │                              │
    │  6. Publish state=verifying  │                              │
    │<─────────────────────────────│                              │
    │                              │                              │
    │  7. Publish state=updating   │                              │
    │<─────────────────────────────│                              │
    │                              │                              │
    │  8. Publish state=rebooting  │                              │
    │<─────────────────────────────│                              │
    │                              │                              │
    │              ┌───────────────┴───────────────┐               │
    │              │         ESP.restart()         │               │
    │              └───────────────┬───────────────┘               │
    │                              │                              │
    │  9. Reconnect, state=idle    │                              │
    │<─────────────────────────────│                              │
    │                              │                              │
    │  10. version=new_version     │                              │
    │<─────────────────────────────│                              │
    │                              │                              │
```

### State Machine

```
                    ┌──────────┐
                    │   IDLE   │◄────────────────────┐
                    └────┬─────┘                     │
                         │ firmware-url/set          │
                         ▼                           │
                    ┌──────────────┐                 │
                    │ DOWNLOADING  │────┐            │
                    └──────┬───────┘    │            │
                           │            │ error      │
                           ▼            │            │
                    ┌──────────────┐    │            │
                    │  VERIFYING   │────┤            │
                    └──────┬───────┘    │            │
                           │            │            │
                           ▼            ▼            │
                    ┌──────────────┐  ┌───────┐     │
                    │   UPDATING   │  │ ERROR │─────┘
                    └──────┬───────┘  └───────┘
                           │
                           ▼
                    ┌──────────────┐
                    │  REBOOTING   │
                    └──────┬───────┘
                           │
                           ▼
                      ESP.restart()
```

---

## Implementation Architecture

### File Structure

Following this SDK's patterns:

```
include/
├── node/
│   └── ota_node.h           # OTA node class header
src/
├── node/
│   └── ota_node.cpp         # OTA node implementation
data/
└── nodes.yaml               # Add OTA node configuration
```

### Class Hierarchy

```
NodeEntity (base class)
    │
    └── OTANode
            ├── NodeProperty _enabled_property
            ├── NodeProperty _state_property
            ├── NodeProperty _progress_property
            ├── NodeProperty _version_property
            ├── NodeProperty _firmware_url_property
            ├── NodeProperty _checksum_property
            └── NodeProperty _message_property
```

### Node Configuration (nodes.yaml)

Add to `data/nodes.yaml`:

```yaml
nodes:
  - id: ota
    name: OTA Update
    type: ota
    properties:
      - id: enabled
        name: OTA Enabled
        datatype: boolean
        settable: true
        retained: true
        value: true
      - id: state
        name: Update State
        datatype: enum
        format: "idle,downloading,verifying,updating,rebooting,error"
        settable: false
        retained: true
      - id: progress
        name: Progress
        datatype: integer
        unit: "%"
        format: "0:100"
        settable: false
        retained: true
      - id: version
        name: Firmware Version
        datatype: string
        settable: false
        retained: true
      - id: firmware-url
        name: Firmware URL
        datatype: string
        settable: true
        retained: false
      - id: checksum
        name: Firmware Checksum
        datatype: string
        settable: true
        retained: false
      - id: message
        name: Status Message
        datatype: string
        settable: false
        retained: true
```

---

## Security Considerations

### 1. HTTPS for Firmware Download

Always use HTTPS when possible:

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
secureClient.setCACert(root_ca);  // Or setInsecure() for testing
HTTPUpdate.update(secureClient, firmware_url);
```

### 2. Firmware Checksum Verification

Verify MD5 or SHA256 before applying:

```cpp
#include <MD5Builder.h>

// During download, compute checksum
MD5Builder md5;
md5.begin();
md5.add(buffer, len);  // For each chunk
md5.calculate();
String computed = md5.toString();

// Compare with provided checksum
if (computed != expected_checksum) {
    Update.abort();
    // Set error state
}
```

### 3. Signed Firmware (Advanced)

ESP32 supports secure boot and signed firmware:

```cpp
// In platformio.ini
build_flags = -DCONFIG_SECURE_SIGNED_ON_UPDATE=y
```

### 4. OTA Enable/Disable

Allow users to disable OTA when not needed:

```cpp
if (!_ota_enabled) {
    set_message("OTA disabled");
    return;
}
```

### 5. Rate Limiting

Prevent rapid repeated update attempts:

```cpp
if (millis() - _last_ota_attempt < OTA_COOLDOWN_MS) {
    set_message("Please wait before retrying");
    return;
}
```

---

## Implementation Steps

### Step 1: Create Header File

Create `include/node/ota_node.h`:

```cpp
#pragma once
#include <node/NodeEntity.h>
#include <HTTPClient.h>
#include <Update.h>

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_UPDATING,
    OTA_STATE_REBOOTING,
    OTA_STATE_ERROR
} OTAState;

class OTANode : public NodeEntity {
public:
    OTANode(const char* homie_node_id);
    ~OTANode() {};

    void loop() override;
    void entity_settable_callback(Property* property) override;

    // OTA operations
    void startUpdate(const char* url);
    void setEnabled(bool enabled);
    bool isEnabled() { return _enabled; }

private:
    // State management
    OTAState _state;
    bool _enabled;
    int _progress;
    char _version[32];
    char _firmware_url[256];
    char _checksum[65];  // SHA256 hex string
    char _message[128];

    // Properties
    NodeProperty _enabled_property;
    NodeProperty _state_property;
    NodeProperty _progress_property;
    NodeProperty _version_property;
    NodeProperty _firmware_url_property;
    NodeProperty _checksum_property;
    NodeProperty _message_property;

    // Internal methods
    void setState(OTAState state);
    void setProgress(int progress);
    void setMessage(const char* msg);
    const char* stateToString(OTAState state);
    bool downloadAndUpdate(const char* url);
    bool verifyChecksum(const char* expected);
};

extern OTANode* otaNode;
```

### Step 2: Create Implementation File

Create `src/node/ota_node.cpp`:

```cpp
#include <node/ota_node.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Firmware version - update with each release
#define FIRMWARE_VERSION "1.0.0"

OTANode* otaNode = nullptr;

OTANode::OTANode(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _state = OTA_STATE_IDLE;
    _enabled = true;
    _progress = 0;
    strcpy(_version, FIRMWARE_VERSION);
    memset(_firmware_url, 0, sizeof(_firmware_url));
    memset(_checksum, 0, sizeof(_checksum));
    memset(_message, 0, sizeof(_message));

    // Setup properties
    _enabled_property.setup_settable(getNodeId(), "enabled",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _state_property.setup(getNodeId(), "state");
    _progress_property.setup(getNodeId(), "progress");
    _version_property.setup(getNodeId(), "version");
    _firmware_url_property.setup_settable(getNodeId(), "firmware-url",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _checksum_property.setup_settable(getNodeId(), "checksum",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _message_property.setup(getNodeId(), "message");

    // Publish initial values
    _enabled_property.set_value_and_publish(_enabled);
    _state_property.set_value_and_publish(stateToString(_state));
    _progress_property.set_value_and_publish(_progress);
    _version_property.set_value_and_publish(_version);
    _message_property.set_value_and_publish("Ready");

    setup();
}

const char* OTANode::stateToString(OTAState state) {
    switch (state) {
        case OTA_STATE_IDLE:        return "idle";
        case OTA_STATE_DOWNLOADING: return "downloading";
        case OTA_STATE_VERIFYING:   return "verifying";
        case OTA_STATE_UPDATING:    return "updating";
        case OTA_STATE_REBOOTING:   return "rebooting";
        case OTA_STATE_ERROR:       return "error";
        default:                    return "unknown";
    }
}

void OTANode::setState(OTAState state) {
    _state = state;
    _state_property.set_value_and_publish(stateToString(state));
}

void OTANode::setProgress(int progress) {
    _progress = progress;
    _progress_property.set_value_and_publish(progress);
}

void OTANode::setMessage(const char* msg) {
    strncpy(_message, msg, sizeof(_message) - 1);
    _message_property.set_value_and_publish(msg);
}

void OTANode::setEnabled(bool enabled) {
    _enabled = enabled;
    _enabled_property.set_value_and_publish(enabled);
}

void OTANode::entity_settable_callback(Property* property) {
    Serial.printf("OTA: settable callback for '%s' = '%s'\n",
                  property->id(), property->value());

    if (strcmp(property->id(), "enabled") == 0) {
        bool val = (strcmp(property->value(), "true") == 0);
        setEnabled(val);
        Serial.printf("OTA: enabled set to %s\n", val ? "true" : "false");
    }
    else if (strcmp(property->id(), "firmware-url") == 0) {
        if (!_enabled) {
            setMessage("OTA is disabled");
            setState(OTA_STATE_ERROR);
            return;
        }
        strncpy(_firmware_url, property->value(), sizeof(_firmware_url) - 1);
        Serial.printf("OTA: firmware URL set to '%s'\n", _firmware_url);
        startUpdate(_firmware_url);
    }
    else if (strcmp(property->id(), "checksum") == 0) {
        strncpy(_checksum, property->value(), sizeof(_checksum) - 1);
        Serial.printf("OTA: checksum set to '%s'\n", _checksum);
    }
}

void OTANode::startUpdate(const char* url) {
    if (_state != OTA_STATE_IDLE) {
        setMessage("Update already in progress");
        return;
    }

    if (strlen(url) == 0) {
        setMessage("No firmware URL provided");
        setState(OTA_STATE_ERROR);
        return;
    }

    Serial.printf("OTA: Starting update from %s\n", url);
    setMessage("Starting download...");

    if (downloadAndUpdate(url)) {
        setState(OTA_STATE_REBOOTING);
        setMessage("Update complete, rebooting...");
        delay(1000);
        ESP.restart();
    }
}

bool OTANode::downloadAndUpdate(const char* url) {
    HTTPClient http;
    WiFiClient client;

    setState(OTA_STATE_DOWNLOADING);
    setProgress(0);

    Serial.printf("OTA: Connecting to %s\n", url);

    if (!http.begin(client, url)) {
        setMessage("Failed to connect to server");
        setState(OTA_STATE_ERROR);
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "HTTP error: %d", httpCode);
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        setMessage("Invalid content length");
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    Serial.printf("OTA: Firmware size: %d bytes\n", contentLength);

    if (!Update.begin(contentLength)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Not enough space: %s", Update.errorString());
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    setState(OTA_STATE_UPDATING);
    setMessage("Writing firmware...");

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int written = 0;
    int lastProgress = 0;

    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        size_t available = stream->available();
        if (available) {
            size_t readBytes = stream->readBytes(buffer,
                min(available, sizeof(buffer)));

            size_t writtenBytes = Update.write(buffer, readBytes);
            if (writtenBytes != readBytes) {
                setMessage("Write error");
                setState(OTA_STATE_ERROR);
                Update.abort();
                http.end();
                return false;
            }

            written += writtenBytes;
            if (contentLength > 0) {
                contentLength -= writtenBytes;
            }

            // Update progress every 5%
            int progress = (written * 100) / http.getSize();
            if (progress >= lastProgress + 5) {
                setProgress(progress);
                lastProgress = progress;
                Serial.printf("OTA: Progress %d%%\n", progress);
            }
        }
        delay(1);
    }

    setProgress(100);

    if (!Update.end(true)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Update failed: %s", Update.errorString());
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    Serial.println("OTA: Update complete!");
    http.end();
    return true;
}

void OTANode::loop() {
    // OTA loop processing
    // Could add timeout handling, retry logic, etc.
}
```

### Step 3: Register OTA Node in Main Application

In `src/main.cpp` or device initialization:

```cpp
#include <node/ota_node.h>

// In setup() or device initialization:
otaNode = new OTANode("ota");
```

### Step 4: Add to nodes.yaml Configuration

Add the OTA node configuration to `data/nodes.yaml` as shown in the Node Configuration section above.

### Step 5: Update platformio.ini

Ensure OTA-compatible partition scheme:

```ini
[env:esp32-s3-devkitc-1]
; ... existing config ...
board_build.partitions = min_spiffs.csv  ; Has OTA partitions
```

### Step 6: Version Management

Create a version header or use build flags:

```ini
; In platformio.ini
build_flags =
    ${common.build_flags}
    -DFIRMWARE_VERSION=\"1.0.0\"
```

---

## Example Code Structure

### Complete OTA Node Header

See `include/node/ota_node.h` in Step 1 above.

### Controller Usage Example

From a Homie controller (extending `controller_example.cpp`):

```cpp
// Trigger OTA update on a device
void trigger_ota_update(const char* device_id, const char* firmware_url) {
    // Optionally set checksum first
    controller_set_property(device_id, "ota", "checksum",
                           "abc123def456...");  // SHA256 hex

    // Trigger update by setting firmware URL
    controller_set_property(device_id, "ota", "firmware-url",
                           firmware_url);
}

// Monitor OTA progress
void monitor_ota_status(const char* device_id) {
    Property* state = controller_get_property(device_id, "ota", "state");
    Property* progress = controller_get_property(device_id, "ota", "progress");
    Property* message = controller_get_property(device_id, "ota", "message");

    if (state && progress && message) {
        Serial.printf("OTA Status: %s (%s%%) - %s\n",
                     state->value(),
                     progress->value(),
                     message->value());
    }
}

// Check device firmware version
void check_device_version(const char* device_id) {
    Property* version = controller_get_property(device_id, "ota", "version");
    if (version) {
        Serial.printf("Device %s running firmware v%s\n",
                     device_id, version->value());
    }
}
```

### Firmware Server Setup

Simple Python HTTP server for testing:

```python
#!/usr/bin/env python3
# firmware_server.py
import http.server
import hashlib
import os

FIRMWARE_DIR = "./firmware"

class FirmwareHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=FIRMWARE_DIR, **kwargs)

if __name__ == "__main__":
    # Calculate checksums for firmware files
    for f in os.listdir(FIRMWARE_DIR):
        if f.endswith('.bin'):
            path = os.path.join(FIRMWARE_DIR, f)
            with open(path, 'rb') as fw:
                checksum = hashlib.sha256(fw.read()).hexdigest()
                print(f"{f}: {checksum}")

    # Start server
    server = http.server.HTTPServer(('0.0.0.0', 8080), FirmwareHandler)
    print("Firmware server running on http://0.0.0.0:8080")
    server.serve_forever()
```

---

## Testing Checklist

- [ ] Device publishes OTA node in `$description`
- [ ] `state` property shows `idle` on boot
- [ ] `version` property shows correct firmware version
- [ ] Setting `enabled` to `false` prevents updates
- [ ] Setting `firmware-url` triggers download
- [ ] `progress` updates during download
- [ ] `state` transitions correctly through states
- [ ] Error conditions set `state` to `error` with message
- [ ] Device reboots after successful update
- [ ] New version is reported after reboot
- [ ] Checksum verification works (when provided)
- [ ] HTTPS downloads work (with valid certificates)

---

## Error Handling

| Error | Message | Recovery |
|-------|---------|----------|
| OTA disabled | "OTA is disabled" | Enable OTA first |
| HTTP error | "HTTP error: {code}" | Check URL, server |
| No space | "Not enough space" | Use smaller partition or firmware |
| Write error | "Write error" | Retry update |
| Checksum mismatch | "Checksum verification failed" | Re-download firmware |
| Timeout | "Download timeout" | Check network, retry |

---

## References

- [ESP32 Arduino OTA Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/ota_web_update.html)
- [Homie Convention 5.0](https://homieiot.github.io/specification/)
- [ESP-IDF OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [PlatformIO ESP32 Partitions](https://docs.platformio.org/en/latest/platforms/espressif32.html#partition-tables)
