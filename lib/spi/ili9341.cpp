// SPDX-License-Identifier: MIT
#include <spi/ili9341.h>
#include <spi/spi_bus.h>
#include <platform/log_stream.h>

// No reset pin: the panel is reset by its software reset command in begin().
Ili9341::Ili9341(const char* id, int pin_sck, int pin_miso, int pin_mosi, int pin_cs, int pin_dc,
                 int rotation, uint32_t spi_hz)
    : Display(id),
      _tft(&SPI, pin_dc, pin_cs, -1),
      _pin_sck(pin_sck), _pin_miso(pin_miso), _pin_mosi(pin_mosi),
      _rotation(rotation), _spi_hz(spi_hz) {}

void Ili9341::setup() {
    ensure_spi_bus(_pin_sck, _pin_miso, _pin_mosi);
    _tft.begin(_spi_hz);
    _tft.setRotation(_rotation);
    _tft.fillScreen(kBlack);
    Serial.printf("ILI9341[%s]: %dx%d at %lu Hz\n", id(), _tft.width(), _tft.height(),
                  (unsigned long)_spi_hz);
}

void Ili9341::do_text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color,
                      uint16_t bg) {
    _tft.setTextSize(size);
    _tft.setTextColor(color, bg);
    _tft.setTextWrap(false);
    _tft.setCursor(x, y);
    _tft.print(s);
}
