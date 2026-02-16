# CLAUDE.md - Project Guidelines for Claude Code

This file contains project-specific instructions for Claude Code when working on the homie-esp32-sdk codebase.

## Platform & Framework

- **Framework**: Arduino for ESP32 using PlatformIO
- **Compatibility**: ESP-IDF callable code that works alongside Arduino framework
- **Targets**: ESP32, ESP32-S3, ESP32-POE-ISO (see platformio.ini for environments)

## Coding Standards

### Memory Management

**NO dynamic allocation** - All objects, buffers, and variables must be:
- Static instantiation (global or static local)
- Stack-allocated (local variables)
- Member variables of statically-allocated objects

```cpp
// CORRECT - static instantiation
BME280 bme280("environment_bme280");
Modbus_SHT20 sht20("environment_sht20", get_serial1());

// CORRECT - stack allocation
char buffer[64];
uint16_t registers[32];

// WRONG - dynamic allocation
BME280* sensor = new BME280("sensor");  // NO
char* buffer = (char*)malloc(64);        // NO
std::vector<int> values;                 // NO
```

### String Handling

**NO Arduino String class** - Use only `const char*` and C-style strings:

```cpp
// CORRECT
const char* node_id = "temperature_sensor";
char topic[128];
snprintf(topic, sizeof(topic), "%s/%s", base, property);

// WRONG
String topic = base + "/" + property;  // NO String class
```

### C++ Features

**NO STL, lambdas, or modern C++ features** unsupported in Arduino ESP32:

```cpp
// WRONG - not supported
std::vector<int> items;                    // NO STL containers
std::function<void()> callback;            // NO std::function
auto lambda = []() { doSomething(); };     // NO lambdas
std::make_unique<Foo>();                   // NO smart pointers

// CORRECT - use C-style alternatives
int items[MAX_ITEMS];
typedef void (*Callback)();
void myCallback() { doSomething(); }
```

### Arduino-Style Pattern

All NodeEntity classes follow Arduino `setup()` and `loop()` pattern:

```cpp
class MyNode : public NodeEntity {
public:
    MyNode(const char* homie_node_id);
    bool setup() override;   // Initialize hardware and properties
    void loop() override;    // Called repeatedly from main loop
};

// Implementation
MyNode::MyNode(const char* homie_node_id) : NodeEntity(homie_node_id) {
    // Constructor: ONLY initialize primitive members
    // NO method calls, NO virtual dispatch, NO property setup
    _value = 0;
    _timestamp = 0;
}

bool MyNode::setup() {
    BaseClass::setup();  // Always call base class first

    // Setup NodeProperties here (requires getNodeId() which needs vtable)
    _my_property.setup(getNodeId(), "property_name");

    // Initialize hardware
    pinMode(MY_PIN, OUTPUT);

    return true;
}

void MyNode::loop() {
    if (pollIntervalTrigger()) {
        // Read sensors, update properties
    }
}
```

### Multi-Core Usage

ESP32 has dual cores. Use them appropriately:

- **Core 0**: Background tasks (Modbus worker, MQTT handling)
- **Core 1**: Main Arduino loop, user code

```cpp
// Example: Start task on specific core
xTaskCreatePinnedToCore(
    modbus_task,        // Task function
    "ModbusTask",       // Name
    4096,               // Stack size
    NULL,               // Parameters
    1,                  // Priority
    &modbus_task_handle,
    0                   // Core 0
);
```

Use FreeRTOS queues for inter-core communication:
```cpp
QueueHandle_t cmd_queue = xQueueCreate(QUEUE_SIZE, sizeof(ModbusCommand));
xQueueSend(cmd_queue, &cmd, portMAX_DELAY);
xQueueReceive(cmd_queue, &cmd, portMAX_DELAY);
```

## Naming Conventions

Follow existing codebase style:

```cpp
// Classes: PascalCase
class ModbusRTUEntity { };
class NodeProperty { };

// Methods: camelCase or snake_case (match surrounding code)
void setup();
void pollAsync();
void set_value_and_publish(float value);

// Member variables: underscore prefix
uint8_t _modbus_address;
Stream* _serial;
NodeProperty _temperature;

// Constants/Macros: SCREAMING_SNAKE_CASE
#define MODBUS_POLLRATE 5000
#define SHT20_ADDR 0x01

// Local variables: snake_case
uint8_t result;
unsigned long timestamp_last_report;
```

## Project Structure

```
homie-esp32-sdk/
├── include/
│   ├── config.h           # Configuration and constants
│   ├── pins.h             # GPIO pin definitions
│   ├── homie/             # Homie protocol implementation
│   ├── modbus/            # Modbus RTU support
│   └── node/              # NodeEntity classes by subsystem
│       ├── gpio/          # GPIO-based nodes (SSR, etc)
│       ├── i2c/           # I2C sensors (BME280, etc)
│       ├── modbus/        # Modbus devices (SHT20, etc)
│       └── spi/           # SPI devices (energy meter)
├── src/
│   ├── main.cpp           # Arduino setup() and loop()
│   ├── homie.cpp          # Node instantiation and setup
│   └── ...                # Implementation files mirror include/
├── data/                  # SPIFFS filesystem (config files)
└── platformio.ini         # Build configuration
```

## Reference Projects

When implementing features, reference these projects for patterns:

- `c:\Users\dougm\git-nesl\homie-esp32-sdk\` - This project (primary)
- `c:\Users\dougm\git-nesl\EVPE_WMAC\` - Related ESP32 project
- `c:\Users\dougm\git\` - Other PlatformIO projects at same folder level

## Testing Guidelines

### When to Add Tests

- Add simple, focused tests for new functionality
- Tests should be isolated and not depend on hardware when possible
- Use serial output for test results on embedded

### Test Implementation

```cpp
// Simple test pattern - can be conditionally compiled
#ifdef RUN_TESTS
void test_modbus_queue() {
    Serial.println("=== TEST: Modbus Queue ===");

    modbus_queue_init();
    assert(modbus_queue_ready());

    ModbusCommand cmd = {.slave_addr = 1, .reg_addr = 0x01};
    uint32_t id = modbus_queue_command(&cmd);
    assert(id > 0);

    Serial.println("PASS: Modbus queue test");
}
#endif
```
## Source Control
delete phantom windows 'nul' file if it appears in git change list

### Stability Rules

- **DO NOT** modify working production code solely to add tests
- **DO NOT** introduce test frameworks that increase binary size
- **DO** add tests alongside new features
- **DO** use compile-time flags to exclude test code from production builds
- **DO** verify existing functionality still works after changes

## Error Handling

Use return codes, not exceptions:

```cpp
// Return codes for operations
uint8_t result = readInputRegisters(reg, count);
if (result != ku8MBSuccess) {
    Serial.printf("Modbus read failed: %d\n", result);
    timestamp_last_failure = millis();
    return false;
}
```

## Serial Debug Output

Use `Serial.printf()` for debug output with consistent formatting:

```cpp
Serial.printf("ModuleName: Action description, param=%d\n", value);
Serial.printf("MODBUS SHT20: Temperature: %2.1fC\n", temp);
Serial.println("=== SUBSYSTEM INIT START ===");
```

## Build Verification

After making changes, verify the build succeeds:

```bash
pio run                           # Build all environments
pio run -e esp32-s3-devkitc-1    # Build specific target
```

Primary targets to verify:
- `esp32-s3-devkitc-1`
- `esp32-poe-iso`

---

**Note to Claude**: After reading and comprehending this CLAUDE.md file, emit a brief acknowledgment in the chat confirming you understand the project guidelines.
