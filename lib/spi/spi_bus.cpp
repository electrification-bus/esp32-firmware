#include <spi/spi_bus.h>
#include <platform/log_stream.h>

static bool _spi_started = false;

SPIClass& ensure_spi_bus(int pin_sck, int pin_miso, int pin_mosi) {
    if (!_spi_started) {
        SPI.begin(pin_sck, pin_miso, pin_mosi);   // no SS: each device drives its own CS
        _spi_started = true;
        Serial.printf("SPI: bus started on SCK=%d MISO=%d MOSI=%d\n", pin_sck, pin_miso, pin_mosi);
    }
    return SPI;
}
