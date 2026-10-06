#!/usr/bin/env python3
"""
Homie Device Test - Standalone MQTT monitor and validator for Homie convention devices.

This test:
1. Connects to MQTT broker and subscribes to device topics
2. Loads expected properties from homie.yml
3. Monitors property values and validates against datatype/format
4. Tests settable properties by publishing to /set topic
5. Reports errors only (not good values)
6. Emits pass/fail summary with diagnostics

Usage:
    python device_test.py [--config config.yml] [--homie homie.yml] [--timeout 30]
                          [--domain ebus|homie] [--host H] [--port N]
                          [--username U] [--password P] [--tls [--cafile F] [--insecure]]
"""

import argparse
import json
import re
import sys
import time
import threading
from dataclasses import dataclass, field
from enum import Enum
from typing import Dict, List, Optional, Any, Callable

# Try to import paho-mqtt
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)

# Try to import PyYAML
try:
    import yaml
except ImportError:
    print("ERROR: PyYAML not installed. Run: pip install pyyaml")
    sys.exit(1)


class LogLevel(Enum):
    DEBUG = 0
    INFO = 1
    WARN = 2
    ERROR = 3
    PASS = 4
    FAIL = 5


class TestResult(Enum):
    PENDING = "PENDING"
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    TIMEOUT = "TIMEOUT"


@dataclass
class PropertySpec:
    """Specification for a Homie property from homie.yml"""
    node_id: str
    property_id: str
    name: str
    datatype: str
    settable: bool = False
    retained: bool = True
    unit: str = ""
    format: str = ""
    value: Any = None  # Expected/default value
    description: str = ""


@dataclass
class PropertyState:
    """Runtime state for a monitored property"""
    spec: PropertySpec
    received_value: Optional[str] = None
    received_at: float = 0
    error_messages: List[str] = field(default_factory=list)
    set_test_result: TestResult = TestResult.PENDING
    set_test_sent_value: Optional[str] = None
    set_test_received_value: Optional[str] = None


@dataclass
class DeviceState:
    """State of the Homie device being tested"""
    device_id: str
    homie_version: str = ""
    name: str = ""
    state: str = ""
    nodes: List[str] = field(default_factory=list)
    properties: Dict[str, PropertyState] = field(default_factory=dict)


class HomieDeviceTest:
    """Main test class for Homie device validation"""

    def __init__(self, config_path: str, homie_path: str, timeout: int = 30,
                 domain: str = "ebus", overrides: Optional[Dict[str, Any]] = None):
        self.config_path = config_path
        self.homie_path = homie_path
        self.timeout = timeout
        self.domain = domain  # topic root: "ebus" with -DUSE_EBUS_TOPIC (default build), else "homie"
        self.overrides = overrides or {}  # command-line mqtt_* values that win over config.yml
        self.start_time = 0

        # MQTT client
        self.client: Optional[mqtt.Client] = None
        self.connected = False

        # Configuration
        self.mqtt_host = "localhost"
        self.mqtt_port = 1883
        self.mqtt_user = ""
        self.mqtt_pass = ""
        self.mqtt_tls = False
        self.mqtt_cafile = None   # None: system CA store
        self.mqtt_insecure = False
        self.device_id = ""
        self.base_topic = f"{domain}/5"  # Homie 5.x topic format: <domain>/5/<device>

        # Device state
        self.device = DeviceState(device_id="")
        self.expected_properties: Dict[str, PropertySpec] = {}

        # Device discovery - collect all devices found via wildcard
        self.discovered_devices: Dict[str, dict] = {}  # device_id -> description
        self.discovery_timeout = 5  # seconds to wait for device discovery
        self.device_discovered = False
        self.current_device_id = ""  # Device currently being tested

        # Test state
        self.errors: List[str] = []
        self.warnings: List[str] = []
        self.set_tests_pending: Dict[str, threading.Event] = {}
        self.all_device_results: Dict[str, bool] = {}  # device_id -> pass/fail

        # Logging
        self.log_level = LogLevel.INFO

    def log(self, level: LogLevel, msg: str):
        """Log a message with timestamp and level"""
        if level.value < self.log_level.value:
            return

        timestamp = time.strftime("%H:%M:%S")
        level_str = level.name.ljust(5)

        # Color coding for terminals
        colors = {
            LogLevel.DEBUG: "\033[90m",  # Gray
            LogLevel.INFO: "\033[0m",    # Default
            LogLevel.WARN: "\033[93m",   # Yellow
            LogLevel.ERROR: "\033[91m",  # Red
            LogLevel.PASS: "\033[92m",   # Green
            LogLevel.FAIL: "\033[91m",   # Red
        }
        reset = "\033[0m"

        color = colors.get(level, "")
        print(f"{color}[{timestamp}] {level_str} {msg}{reset}")

    def load_config(self) -> bool:
        """Load MQTT configuration from config.yml"""
        try:
            with open(self.config_path, 'r') as f:
                cfg = yaml.safe_load(f)

            # config.yml uses device.yml's sections (mqtt: { host: ... }); older generated
            # and hand-maintained files are flat (mqtt_host: ...). Read both, section first —
            # same rule the firmware applies, see cfg_value() in src/platform/config.cpp.
            def get(section, key, flat_key, default):
                sec = cfg.get(section)
                if isinstance(sec, dict) and sec.get(key) is not None:
                    return sec[key]
                value = cfg.get(flat_key)
                return default if value is None else value

            self.mqtt_host = get('mqtt', 'host', 'mqtt_host', 'localhost')
            port = get('mqtt', 'port', 'mqtt_port', None)
            self.mqtt_user = get('mqtt', 'username', 'mqtt_username', '')
            self.mqtt_pass = get('mqtt', 'password', 'mqtt_password', '')
            self.device_id = get('device', 'id', 'device_id', '')

            ov = self.overrides
            if ov.get('host') is not None:
                self.mqtt_host = ov['host']
            if ov.get('port') is not None:
                port = ov['port']
            if ov.get('username') is not None:
                self.mqtt_user = ov['username']
            if ov.get('password') is not None:
                self.mqtt_pass = ov['password']
            if port is not None:
                self.mqtt_port = int(port)
            else:
                self.mqtt_port = 8883 if self.mqtt_tls else 1883

            self.log(LogLevel.INFO, f"Config loaded: {self.mqtt_host}:{self.mqtt_port} device={self.device_id}")
            return True

        except Exception as e:
            self.log(LogLevel.ERROR, f"Failed to load config: {e}")
            return False

    def load_homie_spec(self) -> bool:
        """Load expected properties from homie.yml"""
        try:
            with open(self.homie_path, 'r') as f:
                homie = yaml.safe_load(f)

            self.device.device_id = self.device_id
            self.device.homie_version = str(homie.get('homie', '4.0'))
            self.device.name = homie.get('name', '')

            # Set base topic based on Homie version (<domain>/5 for 5.x, <domain>/4 for 4.x, etc.)
            major_version = self.device.homie_version.split('.')[0]
            self.base_topic = f"{self.domain}/{major_version}"
            self.log(LogLevel.INFO, f"Homie version {self.device.homie_version} -> base topic: {self.base_topic}")

            nodes = homie.get('nodes', [])
            for node in nodes:
                node_id = node.get('id', '')
                self.device.nodes.append(node_id)

                properties = node.get('properties', [])
                for prop in properties:
                    prop_id = prop.get('id', '')
                    key = f"{node_id}/{prop_id}"

                    spec = PropertySpec(
                        node_id=node_id,
                        property_id=prop_id,
                        name=prop.get('name', ''),
                        datatype=prop.get('datatype', 'string'),
                        settable=prop.get('settable', False),
                        retained=prop.get('retained', True),
                        unit=prop.get('unit', ''),
                        format=prop.get('format', ''),
                        value=prop.get('value'),
                        description=prop.get('description', '')
                    )

                    self.expected_properties[key] = spec
                    self.device.properties[key] = PropertyState(spec=spec)

            self.log(LogLevel.INFO, f"Loaded {len(self.expected_properties)} properties from {len(nodes)} nodes")
            return True

        except Exception as e:
            self.log(LogLevel.ERROR, f"Failed to load homie.yml: {e}")
            return False

    def handle_description(self, topic: str, payload: str):
        """Handle $description topic - collect all discovered devices"""
        try:
            # Extract device_id from topic: homie/5/<device_id>/$description
            parts = topic.split('/')
            if len(parts) < 4:
                return
            discovered_device_id = parts[2]

            # Skip if already discovered
            if discovered_device_id in self.discovered_devices:
                return

            desc = json.loads(payload)
            self.discovered_devices[discovered_device_id] = desc
            self.log(LogLevel.INFO, f"Discovered device: '{discovered_device_id}'")

        except json.JSONDecodeError as e:
            self.log(LogLevel.ERROR, f"Failed to parse $description JSON: {e}")
        except Exception as e:
            self.log(LogLevel.ERROR, f"Error processing $description: {e}")

    def setup_device_for_testing(self, device_id: str, description: dict):
        """Setup a specific device for testing - subscribe and validate against homie.yml"""
        self.current_device_id = device_id
        self.device.device_id = device_id
        self.device_discovered = True

        # Subscribe to all topics for this device
        device_topic = f"{self.base_topic}/{device_id}/#"
        self.client.subscribe(device_topic)
        self.log(LogLevel.INFO, f"Subscribed to: {device_topic}")

        # Validate device name matches
        desc_name = description.get('name', '')
        if desc_name and self.device.name and desc_name != self.device.name:
            self.log(LogLevel.WARN, f"Device name mismatch: broker='{desc_name}' homie.yml='{self.device.name}'")

        # Validate homie version matches
        desc_version = str(description.get('homie', ''))
        if desc_version and self.device.homie_version:
            desc_major = desc_version.split('.')[0]
            expected_major = self.device.homie_version.split('.')[0]
            if desc_major != expected_major:
                self.log(LogLevel.WARN, f"Homie version mismatch: broker={desc_version} homie.yml={self.device.homie_version}")

        # Validate nodes exist
        desc_nodes = description.get('nodes', {})
        if isinstance(desc_nodes, dict):
            broker_node_ids = set(desc_nodes.keys())
        elif isinstance(desc_nodes, list):
            broker_node_ids = set(n.get('id', '') for n in desc_nodes)
        else:
            broker_node_ids = set()

        expected_node_ids = set(self.device.nodes)

        missing_nodes = expected_node_ids - broker_node_ids
        extra_nodes = broker_node_ids - expected_node_ids

        if missing_nodes:
            self.log(LogLevel.WARN, f"Nodes in homie.yml but not on broker: {missing_nodes}")
        if extra_nodes:
            self.log(LogLevel.INFO, f"Nodes on broker not in homie.yml: {extra_nodes}")

        self.log(LogLevel.PASS, f"Device '{device_id}' validated")

    def reset_device_state(self):
        """Reset device state for testing next device"""
        self.device.state = ""
        for key in self.device.properties:
            state = self.device.properties[key]
            state.received_value = None
            state.received_at = 0
            state.error_messages = []
            state.set_test_result = TestResult.PENDING
            state.set_test_sent_value = None
            state.set_test_received_value = None

    def validate_value(self, spec: PropertySpec, value: str) -> List[str]:
        """Validate a property value against its specification. Returns list of errors."""
        errors = []

        if value is None or value == "":
            # Empty value might be ok for some types
            return errors

        datatype = spec.datatype.lower()

        try:
            if datatype == "boolean":
                if value.lower() not in ("true", "false", "1", "0", "on", "off", "yes", "no"):
                    errors.append(f"Invalid boolean value: '{value}'")

            elif datatype == "integer":
                int_val = int(value)
                if spec.format:
                    # Format like "0:100" means range
                    match = re.match(r'(-?\d+):(-?\d+)', spec.format)
                    if match:
                        min_val, max_val = int(match.group(1)), int(match.group(2))
                        if int_val < min_val or int_val > max_val:
                            errors.append(f"Integer {int_val} out of range [{min_val}:{max_val}]")

            elif datatype == "float":
                float_val = float(value)
                if spec.format:
                    match = re.match(r'(-?[\d.]+):(-?[\d.]+)', spec.format)
                    if match:
                        min_val, max_val = float(match.group(1)), float(match.group(2))
                        if float_val < min_val or float_val > max_val:
                            errors.append(f"Float {float_val} out of range [{min_val}:{max_val}]")

            elif datatype == "enum":
                if spec.format:
                    valid_values = [v.strip() for v in spec.format.split(',')]
                    if value not in valid_values:
                        errors.append(f"Enum value '{value}' not in allowed values: {valid_values}")

            elif datatype == "color":
                # HSV: h,s,v or RGB: r,g,b
                parts = value.split(',')
                if len(parts) != 3:
                    errors.append(f"Color must have 3 components, got: '{value}'")
                else:
                    try:
                        [int(p) for p in parts]
                    except ValueError:
                        errors.append(f"Color components must be integers: '{value}'")

            elif datatype == "string":
                # String values are always valid
                pass

        except ValueError as e:
            errors.append(f"Value '{value}' cannot be parsed as {datatype}: {e}")

        return errors

    def on_connect(self, client, userdata, flags, rc):
        """MQTT connection callback"""
        if rc == 0:
            self.connected = True
            self.log(LogLevel.INFO, "Connected to MQTT broker")

            # Subscribe to $description with wildcard to discover any Homie 5 device
            desc_topic = f"{self.base_topic}/+/$description"
            client.subscribe(desc_topic)
            self.log(LogLevel.INFO, f"Looking for devices at: {desc_topic}")
        else:
            self.log(LogLevel.ERROR, f"Connection failed with code {rc}")

    def on_message(self, client, userdata, msg):
        """MQTT message callback"""
        topic = msg.topic
        payload = msg.payload.decode('utf-8', errors='replace')

        # Parse topic: homie/5/device-id/node-id/property-id
        # Index:       0     1  2         3       4
        parts = topic.split('/')
        if len(parts) < 4:
            return

        # Handle device-level attributes (homie/5/device-id/$attr)
        if parts[3].startswith('$'):
            attr = parts[3]
            if attr == '$description':
                self.handle_description(topic, payload)
            elif attr == '$state':
                self.device.state = payload
                self.log(LogLevel.INFO, f"Device state: {payload}")
            elif attr == '$nodes':
                # Could validate against homie.yml
                pass
            return

        # Handle node/property topics (homie/5/device-id/node-id/property-id)
        if len(parts) >= 5:
            node_id = parts[3]
            prop_id = parts[4]

            # Skip attribute topics
            if prop_id.startswith('$'):
                return

            key = f"{node_id}/{prop_id}"

            if key in self.device.properties:
                state = self.device.properties[key]
                state.received_value = payload
                state.received_at = time.time()

                # Validate value
                errors = self.validate_value(state.spec, payload)
                if errors:
                    for err in errors:
                        error_msg = f"{key}: {err}"
                        if error_msg not in state.error_messages:
                            state.error_messages.append(error_msg)
                            self.log(LogLevel.ERROR, error_msg)

                # Check if this is a response to a set test
                if key in self.set_tests_pending:
                    state.set_test_received_value = payload
                    if state.set_test_sent_value == payload:
                        state.set_test_result = TestResult.PASS
                    else:
                        # For booleans, normalize comparison
                        if state.spec.datatype == "boolean":
                            sent_bool = state.set_test_sent_value.lower() in ("true", "1", "on", "yes")
                            recv_bool = payload.lower() in ("true", "1", "on", "yes")
                            if sent_bool == recv_bool:
                                state.set_test_result = TestResult.PASS
                            else:
                                state.set_test_result = TestResult.FAIL
                        else:
                            state.set_test_result = TestResult.FAIL

                    self.set_tests_pending[key].set()
            else:
                # Property not in homie.yml
                self.log(LogLevel.WARN, f"Unknown property: {key} = {payload}")

    def connect(self) -> bool:
        """Connect to MQTT broker"""
        try:
            self.client = mqtt.Client(client_id=f"homie_test_{int(time.time())}")
            self.client.on_connect = self.on_connect
            self.client.on_message = self.on_message

            if self.mqtt_user:
                self.client.username_pw_set(self.mqtt_user, self.mqtt_pass)

            if self.mqtt_tls:
                self.client.tls_set(ca_certs=self.mqtt_cafile)
                if self.mqtt_insecure:
                    self.client.tls_insecure_set(True)

            self.log(LogLevel.INFO, f"Connecting to {self.mqtt_host}:{self.mqtt_port}...")
            self.client.connect(self.mqtt_host, self.mqtt_port, 60)
            self.client.loop_start()

            # Wait for connection
            timeout = 10
            while not self.connected and timeout > 0:
                time.sleep(0.5)
                timeout -= 0.5

            return self.connected

        except Exception as e:
            self.log(LogLevel.ERROR, f"Connection failed: {e}")
            return False

    def test_settable_property(self, key: str) -> TestResult:
        """Test a settable property by publishing to its /set topic"""
        state = self.device.properties.get(key)
        if not state or not state.spec.settable:
            return TestResult.SKIP

        spec = state.spec

        # Generate a test value based on datatype
        if spec.datatype == "boolean":
            # Toggle from current or default
            current = state.received_value or str(spec.value) or "false"
            test_value = "false" if current.lower() in ("true", "1", "on", "yes") else "true"
        elif spec.datatype == "integer":
            test_value = "42"
        elif spec.datatype == "float":
            test_value = "42.5"
        elif spec.datatype == "string":
            test_value = "test_value"
        elif spec.datatype == "enum":
            # Use first value from format
            if spec.format:
                test_value = spec.format.split(',')[0].strip()
            else:
                test_value = "unknown"
        else:
            test_value = "test"

        # Setup event to wait for response
        event = threading.Event()
        self.set_tests_pending[key] = event
        state.set_test_sent_value = test_value
        state.set_test_result = TestResult.PENDING

        # Publish to /set topic
        set_topic = f"{self.base_topic}/{self.current_device_id}/{spec.node_id}/{spec.property_id}/set"
        self.log(LogLevel.INFO, f"SET TEST: {key} <- {test_value}")
        self.client.publish(set_topic, test_value)

        # Wait for response (with timeout)
        if event.wait(timeout=5.0):
            if state.set_test_result == TestResult.PASS:
                self.log(LogLevel.PASS, f"SET TEST PASS: {key} = {state.set_test_received_value}")
            else:
                self.log(LogLevel.FAIL, f"SET TEST FAIL: {key} sent={test_value} received={state.set_test_received_value}")
        else:
            state.set_test_result = TestResult.TIMEOUT
            self.log(LogLevel.FAIL, f"SET TEST TIMEOUT: {key} - no response after 5s")

        del self.set_tests_pending[key]
        return state.set_test_result

    def run_tests(self) -> bool:
        """Run all tests and return True if all passed"""
        self.start_time = time.time()

        # Load configuration
        if not self.load_config():
            return False

        # Load homie.yml specification
        if not self.load_homie_spec():
            return False

        # Connect to MQTT
        if not self.connect():
            return False

        # Wait for device discovery via wildcard $description subscription
        self.log(LogLevel.INFO, f"Discovering devices for {self.discovery_timeout} seconds...")
        time.sleep(self.discovery_timeout)

        if not self.discovered_devices:
            self.log(LogLevel.ERROR, f"No Homie devices found at {self.base_topic}/+/$description")
            self.log(LogLevel.ERROR, "Check that device is running and publishing to the correct broker/topic")
            self.client.loop_stop()
            self.client.disconnect()
            return False

        self.log(LogLevel.INFO, f"Found {len(self.discovered_devices)} device(s): {', '.join(self.discovered_devices.keys())}")

        # Test each discovered device
        all_passed = True
        for device_id, description in self.discovered_devices.items():
            self.log(LogLevel.INFO, f"\n{'='*50}")
            self.log(LogLevel.INFO, f"Testing device: {device_id}")
            self.log(LogLevel.INFO, f"{'='*50}")

            # Reset state and setup this device
            self.reset_device_state()
            self.setup_device_for_testing(device_id, description)

            # Wait for initial property messages
            time.sleep(min(5, self.timeout // len(self.discovered_devices)))

            # Run settable property tests
            settable_tests = []
            for key, state in self.device.properties.items():
                if state.spec.settable:
                    # Skip firmware-url and checksum to avoid triggering OTA
                    if state.spec.property_id in ('firmware-url', 'checksum'):
                        self.log(LogLevel.INFO, f"SKIP: {key} (OTA trigger)")
                        state.set_test_result = TestResult.SKIP
                        continue

                    result = self.test_settable_property(key)
                    settable_tests.append((key, result))

            # Wait for more property messages
            time.sleep(2)

            # Generate report for this device
            device_passed = self.generate_report(settable_tests)
            self.all_device_results[device_id] = device_passed
            if not device_passed:
                all_passed = False

            # Unsubscribe from this device
            self.client.unsubscribe(f"{self.base_topic}/{device_id}/#")

        # Disconnect
        self.client.loop_stop()
        self.client.disconnect()

        # Print overall summary
        self.print_overall_summary()

        return all_passed

    def print_overall_summary(self):
        """Print summary of all devices tested"""
        if len(self.all_device_results) > 1:
            print("\n" + "="*70)
            print("OVERALL SUMMARY - ALL DEVICES")
            print("="*70)
            for device_id, passed in self.all_device_results.items():
                status = "PASS" if passed else "FAIL"
                color = "\033[92m" if passed else "\033[91m"
                reset = "\033[0m"
                print(f"{color}  {device_id}: {status}{reset}")

            total = len(self.all_device_results)
            passed = sum(1 for p in self.all_device_results.values() if p)
            print(f"\nTotal: {passed}/{total} devices passed")
            print("="*70 + "\n")

    def generate_report(self, settable_tests: List[tuple]) -> bool:
        """Generate test report and return True if all tests passed"""
        print("\n" + "="*70)
        print("HOMIE DEVICE TEST REPORT")
        print("="*70)

        print(f"\nDevice: {self.current_device_id}")
        print(f"Base Topic: {self.base_topic}/{self.current_device_id}")
        print(f"Homie Version: {self.device.homie_version}")
        print(f"Device State: {self.device.state}")
        print(f"Device Discovered: {'Yes' if self.device_discovered else 'No'}")
        print(f"Nodes (from homie.yml): {', '.join(self.device.nodes)}")

        # Property reception stats
        print(f"\n--- Property Status ---")
        received = 0
        missing = []
        error_count = 0

        for key, state in self.device.properties.items():
            if state.received_value is not None:
                received += 1
            else:
                missing.append(key)

            error_count += len(state.error_messages)

        print(f"Properties expected: {len(self.device.properties)}")
        print(f"Properties received: {received}")

        if missing:
            print(f"\nMISSING properties (no value received):")
            for key in missing:
                print(f"  - {key}")

        # Validation errors
        if error_count > 0:
            print(f"\n--- Validation Errors ({error_count}) ---")
            for key, state in self.device.properties.items():
                for err in state.error_messages:
                    print(f"  ERROR: {err}")

        # Settable property test results
        if settable_tests:
            print(f"\n--- Settable Property Tests ---")
            pass_count = 0
            fail_count = 0
            skip_count = 0

            for key, result in settable_tests:
                if result == TestResult.PASS:
                    pass_count += 1
                elif result == TestResult.SKIP:
                    skip_count += 1
                else:
                    fail_count += 1
                    state = self.device.properties[key]
                    print(f"  FAIL: {key}")
                    print(f"         Sent: {state.set_test_sent_value}")
                    print(f"         Received: {state.set_test_received_value}")

            print(f"\nSettable tests: {pass_count} passed, {fail_count} failed, {skip_count} skipped")

        # Final verdict
        print("\n" + "="*70)

        all_passed = True

        if missing:
            print("WARN: Some expected properties were not received")
            # Not necessarily a failure - device might not have all nodes active

        if error_count > 0:
            print(f"FAIL: {error_count} validation error(s) detected")
            all_passed = False

        if settable_tests:
            fails = [t for t in settable_tests if t[1] not in (TestResult.PASS, TestResult.SKIP)]
            if fails:
                print(f"FAIL: {len(fails)} settable property test(s) failed")
                all_passed = False

        if self.device.state not in ('ready', 'init'):
            print(f"WARN: Device state is '{self.device.state}' (expected 'ready' or 'init')")

        if all_passed:
            print("\n*** ALL TESTS PASSED ***")
        else:
            print("\n*** TESTS FAILED ***")

        print("="*70 + "\n")

        return all_passed


def main():
    parser = argparse.ArgumentParser(description='Homie Device Test')
    parser.add_argument('--config', default='data/config.yml', help='Path to config.yml')
    parser.add_argument('--homie', default='data/homie.yml', help='Path to homie.yml')
    parser.add_argument('--timeout', type=int, default=30, help='Test timeout in seconds')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--domain', default='ebus', choices=['ebus', 'homie'],
                        help='Topic domain: "ebus" (default; firmware built with -DUSE_EBUS_TOPIC) '
                             'or "homie" (firmware built without it)')
    parser.add_argument('--host', help='MQTT broker host (overrides config.yml)')
    parser.add_argument('--port', type=int,
                        help='MQTT broker port (overrides config.yml; else 8883 with --tls, 1883 without)')
    parser.add_argument('--username', help='MQTT username (overrides config.yml)')
    parser.add_argument('--password', help='MQTT password (overrides config.yml)')
    parser.add_argument('--tls', action='store_true', help='Connect to the broker over TLS')
    parser.add_argument('--cafile', help='CA certificate for --tls (default: system CA store)')
    parser.add_argument('--insecure', action='store_true',
                        help='With --tls, skip broker hostname verification')

    args = parser.parse_args()
    if (args.cafile or args.insecure) and not args.tls:
        parser.error('--cafile and --insecure require --tls')

    overrides = {'host': args.host, 'port': args.port,
                 'username': args.username, 'password': args.password}
    test = HomieDeviceTest(args.config, args.homie, args.timeout, args.domain, overrides)
    test.mqtt_tls = args.tls
    test.mqtt_cafile = args.cafile
    test.mqtt_insecure = args.insecure

    if args.verbose:
        test.log_level = LogLevel.DEBUG

    success = test.run_tests()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
