// SPDX-License-Identifier: MIT
#pragma once
#include <Adafruit_ILI9341.h>
#include <peripheral/display.h>

// An ILI9341 320x240 TFT on the shared SPI bus, write-only (no MISO needed), e.g. the
// Olimex MOD-LCD2.8RTP. A peripheral: list it under peripherals:, not nodes:.
// native() is the Adafruit_ILI9341 (an Adafruit_GFX) for drawing beyond Display; hold a
// PeripheralLock while using it.
class Ili9341 : public Display {
public:
    Ili9341(const char* id, int pin_sck, int pin_miso, int pin_mosi, int pin_cs, int pin_dc,
            int rotation, uint32_t spi_hz);

    Adafruit_ILI9341& native() { return _tft; }

    void setup() override;

protected:
    int16_t do_width() override { return _tft.width(); }
    int16_t do_height() override { return _tft.height(); }
    uint8_t do_rotation() override { return _tft.getRotation(); }
    // Adafruit GFX's built-in font: 6x8 pixels per character at size 1, scaled by size.
    int16_t do_char_width(uint8_t size) override { return 6 * size; }
    int16_t do_char_height(uint8_t size) override { return 8 * size; }
    void do_clear(uint16_t color) override { _tft.fillScreen(color); }
    void do_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        _tft.fillRect(x, y, w, h, color);
    }
    void do_text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color,
                 uint16_t bg) override;
    void do_flush() override {}   // draws straight to the panel

private:
    Adafruit_ILI9341 _tft;
    int _pin_sck, _pin_miso, _pin_mosi;
    int _rotation;
    uint32_t _spi_hz;
};
