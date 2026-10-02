#pragma once
#include <platform/modbus/ModbusRTUEntity.h>

// A Modbus RTU device described by its register map, not by C++.
//
// Most simple Modbus sensors are "read these registers, scale them, publish them". That
// is what every driver in lib/modbus does; lib/modbus/sht20.cpp is 94 lines of which the
// device-specific part is two register addresses and a /10 scale. This component takes
// that map from device.yml instead, so a new sensor needs no code at all:
//
//   - component: modbus/register_device
//     id: env1
//     config: { address: 1 }          # RS485 pins come from the buses.rs485 block
//     registers:
//       - { id: temperature,       reg: 1, scale: 0.1, signed: true, unit: "°C" }
//       - { id: relative-humidity, reg: 2, scale: 0.1, unit: "%" }
//
// `ebus-esp32 generate` turns that list into BOTH the Homie properties and the static
// ModbusRegisterDesc table below, so there is no runtime parsing and no heap.
//
// Write a real driver (sht20 is the worked example) when the device needs more than this:
// multi-register values, non-linear decoding, writes, or a command sequence.

// REGDEV_ prefix, not MODBUS_: modbus_queue.h already defines MODBUS_MAX_REGISTERS (32)
// for the response buffer, and redefining it here silently changed its meaning for
// anything compiled afterwards.
#define REGDEV_MAX_REGISTERS 16

// How the scaled value reaches MQTT. This must agree with the Homie `datatype` the
// generator puts in $description — publishing "1.0000" for a property declared boolean
// is a payload a strict controller should reject.
enum ModbusRegDatatype : uint8_t {
    REGDEV_FLOAT = 0,       // the default
    REGDEV_INTEGER,
    REGDEV_BOOLEAN,
};

struct ModbusRegisterDesc {
    const char* id;         // Homie property id
    uint16_t    reg;        // register address
    bool        holding;    // false = input register (0x04), true = holding (0x03)
    bool        is_signed;  // interpret the raw 16-bit value as signed
    float       scale;      // published value = raw * scale
    uint8_t     datatype;   // ModbusRegDatatype — must match the declared Homie datatype
};

// One Modbus transaction covering a contiguous, same-function span of registers.
// Built once in setup(); see the comment on plan_runs() for why the map is not simply
// read entry by entry.
struct ModbusRun {
    uint16_t start_reg;   // address of the first register in the span
    uint8_t  first;       // index into _order[] of the first descriptor
    uint8_t  count;       // registers in the span (also the response word count)
    bool     holding;     // function code for the whole span
    uint32_t cmd_id;      // async: id of the outstanding queued read, 0 when none
};

class ModbusRegisterDevice : public ModbusRTUEntity {
public:
    // useAsyncQueue defaults true: the queued path does not block loop() on RS485
    // turnaround, which at 9600 baud is milliseconds per transaction with MQTT, the
    // captive portal and the log tee all sharing the same task.
    ModbusRegisterDevice(const char* homie_node_id, Stream& serial,
                         int pin_rs485_a, int pin_rs485_b, uint8_t addr,
                         const ModbusRegisterDesc* regs, size_t count,
                         bool useAsyncQueue = true);
    ~ModbusRegisterDevice() {}

    bool setup() override;
    uint8_t poll() override;                                  // sync path
    void pollAsync() override;                                // async path
    void handleAsyncResponse(ModbusResponse* resp) override;

private:
    void plan_runs();
    void publish_one(uint8_t desc_index, uint16_t raw);

    const ModbusRegisterDesc* _regs;
    size_t _count;
    NodeProperty _props[REGDEV_MAX_REGISTERS];

    // Descriptor indices sorted by (function, address) and the spans they collapse into.
    uint8_t   _order[REGDEV_MAX_REGISTERS];
    ModbusRun _runs[REGDEV_MAX_REGISTERS];   // worst case: nothing merges, one run each
    uint8_t   _num_runs = 0;
};
