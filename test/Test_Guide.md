# Homie Device Test Guide

A standalone Python test tool for validating Homie convention MQTT devices.

## Overview

This test tool:
1. Connects to your MQTT broker
2. **Discovers ALL devices** by subscribing to `homie/5/+/$description` (wildcard matches any device)
3. Collects all devices found during a 5-second discovery window
4. **Iterates over each discovered device** and runs the full test suite:
   - Validates `$description` matches expected nodes from `homie.yml`
   - Subscribes to all topics for the device
   - Monitors property values in real-time
   - Validates values against `homie.yml` specifications (datatype, format, range)
   - Tests settable properties by publishing to `/set` topics
5. Reports **errors only** (good values are not logged)
6. Generates a pass/fail summary for each device and an overall summary

**Workflow:**
```
┌─────────────┐     MQTT      ┌─────────────┐
│   ESP32     │ ───────────▶  │   Broker    │
│  (Device)   │               │  (shiftr.io)│
└─────────────┘               └──────┬──────┘
                                     │
                              ┌──────▼──────┐
                              │ Python Test │
                              │    (PC)     │
                              └─────────────┘
```

### MQTT Topic Structure

The test automatically uses the correct topic format based on the Homie version in `homie.yml`:

| Homie Version | Base Topic | Example Property Topic |
|--------------|------------|------------------------|
| 5.x | `homie/5` | `homie/5/esp32poeiso/relay1/state` |
| 4.x | `homie/4` | `homie/4/esp32poeiso/relay1/state` |

## Requirements

### Python Dependencies

```bash
pip install paho-mqtt pyyaml
```

### Device Requirements

- ESP32 device running homie-esp32-sdk firmware
- Device connected to MQTT broker
- `homie.yml` file describing expected nodes/properties
- `config.yml` with MQTT broker settings

## Quick Start

### 1. Ensure Device is Running

Power on your ESP32 device and verify it connects to MQTT. The 'name' property of the device is in the topic, for example 'esp32poeiso' in the folliwing example:

```bash
# Monitor MQTT topics (optional - requires mosquitto-clients)
# Homie 5.x uses topic format: homie/5/<device>/#
mosquitto_sub -h public.cloud.shiftr.io -u public -P public -t "homie/5/esp32poeiso/#" -v
```

### 2. Run the Test

From the project root directory:

```bash
# Basic test (30 second timeout)
python test/device_test.py

# With custom timeout
python test/device_test.py --timeout 60

# With verbose logging
python test/device_test.py -v --timeout 45

# With custom config paths
python test/device_test.py --config data/config.yml --homie data/homie.yml
```

### 3. Observe Output

The test produces real-time logging followed by a summary report:

```
[14:32:01] INFO  Config loaded: public.cloud.shiftr.io:1883 device=esp32poeiso
[14:32:01] INFO  Homie version 5.0.1 -> base topic: homie/5
[14:32:01] INFO  Loaded 15 properties from 10 nodes
[14:32:01] INFO  Connecting to public.cloud.shiftr.io:1883...
[14:32:02] INFO  Connected to MQTT broker
[14:32:02] INFO  Looking for devices at: homie/5/+/$description
[14:32:02] INFO  Discovering devices for 5 seconds...
[14:32:03] INFO  Discovered device: 'esp32poeiso'
[14:32:05] INFO  Discovered device: 'esp32cam'
[14:32:07] INFO  Found 2 device(s): esp32poeiso, esp32cam

[14:32:07] INFO  ==================================================
[14:32:07] INFO  Testing device: esp32poeiso
[14:32:07] INFO  ==================================================
[14:32:07] INFO  Subscribed to: homie/5/esp32poeiso/#
[14:32:07] PASS  Device 'esp32poeiso' validated
[14:32:08] INFO  SET TEST: relay1/state <- true
[14:32:08] PASS  SET TEST PASS: relay1/state = true
[14:32:09] ERROR environment_sht20/temperature: Float 999.9 out of range [0:100]

======================================================================
HOMIE DEVICE TEST REPORT
======================================================================

Device: esp32poeiso
Base Topic: homie/5/esp32poeiso
Homie Version: 5.0.1
Device State: ready
...

*** TESTS FAILED ***
======================================================================

[14:32:15] INFO  ==================================================
[14:32:15] INFO  Testing device: esp32cam
[14:32:15] INFO  ==================================================
...

======================================================================
OVERALL SUMMARY - ALL DEVICES
======================================================================
  esp32poeiso: FAIL
  esp32cam: PASS

Total: 1/2 devices passed
======================================================================
```

## Command Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--config` | `data/config.yml` | Path to MQTT configuration file |
| `--homie` | `data/homie.yml` | Path to Homie device specification |
| `--timeout` | `30` | Test duration in seconds |
| `-v, --verbose` | off | Enable debug-level logging |

## Understanding the Output

### Log Levels

| Level | Color | Meaning |
|-------|-------|---------|
| DEBUG | Gray | Detailed diagnostic info (verbose mode only) |
| INFO | White | Normal operation messages |
| WARN | Yellow | Potential issues, not failures |
| ERROR | Red | Validation errors, value problems |
| PASS | Green | Test passed |
| FAIL | Red | Test failed |

### Error Types

#### Validation Errors

Values that don't match the `homie.yml` specification:

```
ERROR: ota/progress: Integer 150 out of range [0:100]
ERROR: ota/state: Enum value 'unknown' not in allowed values: ['idle', 'downloading', ...]
ERROR: relay1/state: Invalid boolean value: 'maybe'
```

#### Missing Properties

Properties defined in `homie.yml` but never received via MQTT:

```
MISSING properties (no value received):
  - meter_volts/volts
  - meter_current/amps
```

This may indicate:
- Node not instantiated in firmware
- Node not publishing values
- MQTT connectivity issues

#### Settable Test Failures

When a `/set` command doesn't result in expected value:

```
FAIL: led/on_off
       Sent: true
       Received: None
```

This may indicate:
- Property not actually settable in firmware
- Callback not implemented
- Value rejected by device logic

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |

## Configuration Files

### config.yml

MQTT broker connection settings:

```yaml
config:
  - mqtt_host: public.cloud.shiftr.io
    mqtt_port: 1883
    mqtt_username: public
    mqtt_password: public
    device_id: esp32poeiso  # For reference only - test discovers all devices via wildcard
```

Note: The `device_id` field is used for reference/logging but the test discovers and tests **all** Homie devices found on the broker via wildcard subscription.

### homie.yml

Expected device structure and property specifications:

```yaml
homie: 5.0.1
name: ESP32POEISO
nodes:
  - id: relay1
    name: Solid State Relay
    type: gpio
    properties:
      - id: state
        name: State
        datatype: boolean    # Validates true/false/1/0/on/off
        settable: true       # Will be tested with SET command
        retained: true
        value: false         # Default/expected value

  - id: ota
    properties:
      - id: progress
        datatype: integer
        format: "0:100"      # Validates range [0-100]

      - id: state
        datatype: enum
        format: "idle,downloading,verifying,updating,rebooting,error"
```

### Supported Datatypes

| Datatype | Validation |
|----------|------------|
| `boolean` | Must be true/false/1/0/on/off/yes/no |
| `integer` | Must parse as int; optional range via `format: "min:max"` |
| `float` | Must parse as float; optional range via `format: "min:max"` |
| `string` | Any value accepted |
| `enum` | Must match one of values in `format: "val1,val2,val3"` |
| `color` | Must be 3 comma-separated integers (HSV or RGB) |

## Troubleshooting

### No Devices Found

```
ERROR: No Homie devices found at homie/5/+/$description
ERROR: Check that device is running and publishing to the correct broker/topic
```

This means no `$description` was found on the broker during the 5-second discovery window:
- ESP32 device may not be running or connected to WiFi
- Device is publishing to a different broker
- Device is using different Homie version (check topic prefix)
- Device has not yet published its `$description` (try increasing discovery timeout)

**Debug steps:**
```bash
# Check if device is publishing anything
mosquitto_sub -h YOUR_BROKER -t "homie/#" -v

# Look specifically for your device
mosquitto_sub -h YOUR_BROKER -t "homie/5/esp32poeiso/#" -v
```

### Connection Refused

```
ERROR: Connection failed: [Errno 111] Connection refused
```

- Check MQTT broker is running
- Verify host/port in `config.yml`
- Check firewall rules

### No Messages Received

```
Properties expected: 15
Properties received: 0
```

- Device may not be running
- Device ID mismatch between `config.yml` and actual device
- Device using different MQTT broker
- Topic prefix mismatch (Homie 5.x uses `homie/5/`, Homie 4.x uses `homie/4/`)

### Settable Test Timeout

```
FAIL: SET TEST TIMEOUT: relay1/state - no response after 5s
```

- Property callback not implemented in firmware
- Device not subscribed to `/set` topic
- Network latency issues

### All Properties Missing

Check device is publishing:

```bash
# Subscribe to all Homie 5.x topics
mosquitto_sub -h YOUR_BROKER -t "homie/5/#" -v

# Or all Homie topics (any version)
mosquitto_sub -h YOUR_BROKER -t "homie/#" -v
```

If nothing appears, the device isn't connected or isn't publishing.

## Integration with CI/CD

The test returns exit code 0 on success, 1 on failure:

```bash
# Run test and check result
python test/device_test.py --timeout 60
if [ $? -eq 0 ]; then
    echo "Device test passed"
else
    echo "Device test failed"
    exit 1
fi
```

## Extending the Test

### Adding Custom Validation

Edit `validate_value()` in `device_test.py` to add custom validation rules:

```python
elif datatype == "temperature":
    temp = float(value)
    if temp < -40 or temp > 85:
        errors.append(f"Temperature {temp} out of sensor range [-40:85]")
```

### Skipping Specific Tests

Properties are skipped automatically:
- `firmware-url` and `checksum` (would trigger OTA)

Add more skips in `run_tests()`:

```python
if state.spec.property_id in ('firmware-url', 'checksum', 'dangerous-prop'):
    state.set_test_result = TestResult.SKIP
    continue
```

## Related Documentation

- [Homie Convention](https://homieiot.github.io/) - Official Homie MQTT convention
- [homie-esp32-sdk](../README.md) - Main project documentation
- [CLAUDE.md](../CLAUDE.md) - Coding standards for this project
