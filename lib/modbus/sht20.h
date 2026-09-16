#pragma once

#include <modbus/register_device.h>

// SHT20 temperature/humidity over Modbus RTU.
//
// The device is two input registers scaled by 10, which is exactly what
// modbus/register_device expresses — so this is a NAMED PROFILE over it rather than its
// own polling implementation: a static register table and a constructor.
//
// It stays a component in its own right (rather than asking every user to write the map
// into device.yml) because the part is common and the name is the documentation:
//
//   - component: modbus/sht20
//     id: environment
//     config: { pin_rs485_a: 36, pin_rs485_b: 4 }
//
// Reach for register_device directly for a device with no profile here; write a real
// driver when a map is not enough — values spanning two registers, non-linear decoding,
// writes, or a command sequence.

class Modbus_SHT20 : public ModbusRegisterDevice {
    public:
        Modbus_SHT20(const char* homie_node_id, Stream& serial,
                     int pin_rs485_a, int pin_rs485_b,
                     uint8_t addr = SHT20_ADDR, bool useAsyncQueue = true);
        ~Modbus_SHT20() {};
};
