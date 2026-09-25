#pragma once
#include <node/NodeEntity.h>
#include <spi/spi_bus.h>

// Base for every spi/* driver — the SPI counterpart of I2CEntity and ModbusRTUEntity.
// It owns the bus pins (from device.yml's buses.spi block) and its setup() brings up the
// shared bus; a driver never calls ensure_spi_bus() itself. Chip-select pins are per
// device, so they belong to the driver, as does binding its device library to the bus
// (most Arduino SPI libraries just use the global SPI object, so that is usually nothing).
class SPIEntity : public NodeEntity {
    public:
        SPIEntity(const char* homie_id, int pin_sck, int pin_miso, int pin_mosi)
            : NodeEntity(homie_id), _sck(pin_sck), _miso(pin_miso), _mosi(pin_mosi) {}

        // NodeEntity::setup(), then the shared bus. Derived setup() calls this first.
        bool setup() override {
            NodeEntity::setup();
            _spi = &ensure_spi_bus(_sck, _miso, _mosi);
            return true;
        }

        SPIClass& spi() const { return *_spi; }   // valid after setup()

    private:
        int       _sck;
        int       _miso;
        int       _mosi;
        SPIClass* _spi = nullptr;
};
