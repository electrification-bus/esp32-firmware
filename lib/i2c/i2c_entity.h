#pragma once
#include <node/NodeEntity.h>
#include <i2c/i2c_bus.h>

// Base for every i2c/* driver — the I2C counterpart of ModbusRTUEntity. It owns the bus
// pins (from device.yml's buses.i2c block) and the device address, and its setup()
// brings up the shared bus; a driver never calls ensure_i2c_bus() itself. Binding the
// device library to that bus stays in the driver, because each library does it its own
// way (Adafruit: begin(address(), &wire())).
class I2CEntity : public NodeEntity {
    public:
        I2CEntity(const char* homie_id, int pin_sda, int pin_scl, uint8_t address)
            : NodeEntity(homie_id), _sda(pin_sda), _scl(pin_scl), _address(address) {}

        // NodeEntity::setup(), then the shared bus. Derived setup() calls this first.
        bool setup() override {
            NodeEntity::setup();
            _wire = &ensure_i2c_bus(_sda, _scl);
            return true;
        }

        TwoWire& wire() const    { return *_wire; }   // valid after setup()
        uint8_t  address() const { return _address; }

    private:
        int      _sda;
        int      _scl;
        uint8_t  _address;
        TwoWire* _wire = nullptr;
};
