// SPDX-License-Identifier: MIT
#pragma once
#include <Adafruit_GFX.h>
#include <peripheral/display.h>

typedef struct esp_lcd_panel_t* esp_lcd_panel_handle_t;

// Adafruit GFX drawing straight into a DPI panel's 24-bit frame buffer, rotated as set:
// each RGB565 color is widened as it is written. It records the panel rows it changes, so
// a flush writes back only those from the cache.
class FrameBufferGfx : public Adafruit_GFX {
public:
    FrameBufferGfx(int16_t w, int16_t h) : Adafruit_GFX(w, h) {}
    void attach(uint8_t* fb) { _fb = fb; }
    bool attached() const { return _fb != nullptr; }
    uint8_t* buffer() { return _fb; }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        fillRect(x, y, w, 1, color);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        fillRect(x, y, 1, h, color);
    }
    void fillScreen(uint16_t color) override { fillRect(0, 0, _width, _height, color); }

    // The panel rows [*y0, *y1) changed since the last call; false when none did.
    bool take_dirty(int16_t* y0, int16_t* y1);

private:
    void mark(int16_t py) {
        if (py < _dirty_y0) _dirty_y0 = py;
        if (py + 1 > _dirty_y1) _dirty_y1 = py + 1;
    }

    void put(int32_t i, uint16_t color);

    uint8_t* _fb = nullptr;
    int16_t _dirty_y0 = INT16_MAX;
    int16_t _dirty_y1 = 0;
};

// An ST7701 480x640 panel on the ESP32-P4's MIPI-DSI, one data lane: the Olimex
// MIPI-LCD2.8-640x480 V2. Drawing goes into a frame buffer in PSRAM that the DSI host
// refreshes the panel from; flush() makes what was drawn visible. A peripheral: list it
// under peripherals:, not nodes:. native() is the Adafruit_GFX over the frame buffer; hold
// a PeripheralLock while using it, then call flush().
class St7701 : public Display {
public:
    St7701(const char* id, int rotation, int lane_mbps, int dpi_mhz);

    Adafruit_GFX& native() { return _gfx; }

    void setup() override;

protected:
    int16_t do_width() override { return _gfx.width(); }
    int16_t do_height() override { return _gfx.height(); }
    uint8_t do_rotation() override { return _gfx.getRotation(); }
    // Adafruit GFX's built-in font: 6x8 pixels per character at size 1, scaled by size.
    int16_t do_char_width(uint8_t size) override { return 6 * size; }
    int16_t do_char_height(uint8_t size) override { return 8 * size; }
    void do_clear(uint16_t color) override { _gfx.fillScreen(color); }
    void do_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        _gfx.fillRect(x, y, w, h, color);
    }
    void do_text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color,
                 uint16_t bg) override;
    void do_flush() override;

private:
    FrameBufferGfx _gfx;
    esp_lcd_panel_handle_t _panel = nullptr;
    int _rotation, _lane_mbps, _dpi_mhz;
};
