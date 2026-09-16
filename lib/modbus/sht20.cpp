#include <Arduino.h>
#include <modbus/sht20.h>

// Input registers 1 and 2, both tenths. Temperature is signed — it reads below zero, and
// treating it as unsigned turns -0.1 °C into 6553.5.
//
// Contiguous, so register_device collapses them into the single two-register read this
// driver has always issued.
static const ModbusRegisterDesc SHT20_REGS[] = {
    { "temperature",       1, false, true,  0.1f },
    { "relative_humidity", 2, false, false, 0.1f },
};

Modbus_SHT20::Modbus_SHT20(const char* homie_node_id, Stream& serial,
                           int pin_rs485_a, int pin_rs485_b,
                           uint8_t addr, bool useAsyncQueue)
    : ModbusRegisterDevice(homie_node_id, serial, pin_rs485_a, pin_rs485_b, addr,
                           SHT20_REGS, sizeof(SHT20_REGS) / sizeof(SHT20_REGS[0]),
                           useAsyncQueue) {}
