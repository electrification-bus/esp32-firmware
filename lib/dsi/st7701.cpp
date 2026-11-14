// SPDX-License-Identifier: MIT
#include <dsi/st7701.h>

#include <Arduino.h>
#include <soc/soc_caps.h>

#if !SOC_MIPI_DSI_SUPPORTED
#error "dsi/st7701 needs a chip with MIPI-DSI (the ESP32-P4)"
#endif

#include <esp_cache.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>

namespace {

constexpr int kPanelWidth = 480;
constexpr int kPanelHeight = 640;
// The P4's MIPI PHY runs from internal LDO channel 3 at 2.5 V.
constexpr int kPhyLdoChannel = 3;
constexpr int kPhyLdoMv = 2500;

struct InitCmd {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
};

// The panel's vendor initialization (ST7701 command banks 0x13, 0x10 and 0x11), from
// esp_panel_board_custom_conf.h in Olimex's ESP32-P4-DevKit-MIPI-LCD-Arduino demo
// (https://github.com/OLIMEX/ESP32-P4-DevKit), a file tagged
// SPDX-License-Identifier: Apache-2.0 (THIRD_PARTY_LICENSES.md).
const InitCmd kVendorInit[] = {
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}},
    {0xEF, 1, {0x08}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}},
    {0xC0, 2, {0x4f, 0x00}},
    {0xC1, 2, {0x10, 0x0c}},
    {0xC2, 2, {0x07, 0x14}},
    {0xCC, 1, {0x10}},
    {0xB0, 16, {0x0a, 0x18, 0x1e, 0x12, 0x16, 0x0c, 0x0e, 0x0d,
                0x0c, 0x29, 0x06, 0x14, 0x13, 0x29, 0x33, 0x1c}},
    {0xB1, 16, {0x0a, 0x19, 0x21, 0x0a, 0x0c, 0x00, 0x0c, 0x03,
                0x03, 0x23, 0x01, 0x0e, 0x0c, 0x27, 0x2b, 0x1c}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}},
    {0xB0, 1, {0x5d}},
    {0xB1, 1, {0x61}},
    {0xB2, 1, {0x84}},
    {0xB3, 1, {0x80}},
    {0xB5, 1, {0x4d}},
    {0xB7, 1, {0x85}},
    {0xB8, 1, {0x20}},
    {0xC1, 1, {0x78}},
    {0xC2, 1, {0x78}},
    {0xD0, 1, {0x88}},
    {0xE0, 3, {0x00, 0x00, 0x02}},
    {0xE1, 11, {0x06, 0xa0, 0x08, 0xa0, 0x05, 0xa0, 0x07, 0xa0, 0x00, 0x44, 0x44}},
    {0xE2, 12, {0x20, 0x20, 0x44, 0x44, 0x96, 0xa0, 0x00, 0x00, 0x96, 0xa0, 0x00, 0x00}},
    {0xE3, 4, {0x00, 0x00, 0x22, 0x22}},
    {0xE4, 2, {0x44, 0x44}},
    {0xE5, 16, {0x0d, 0x91, 0xa0, 0xa0, 0x0f, 0x93, 0xa0, 0xa0,
                0x09, 0x8d, 0xa0, 0xa0, 0x0b, 0x8f, 0xa0, 0xa0}},
    {0xE6, 4, {0x00, 0x00, 0x22, 0x22}},
    {0xE7, 2, {0x44, 0x44}},
    {0xE8, 16, {0x0c, 0x90, 0xa0, 0xa0, 0x0e, 0x92, 0xa0, 0xa0,
                0x08, 0x8c, 0xa0, 0xa0, 0x0a, 0x8e, 0xa0, 0xa0}},
    {0xE9, 2, {0x36, 0x00}},
    {0xEB, 7, {0x00, 0x01, 0xe4, 0xe4, 0x44, 0x88, 0x40}},
    {0xED, 16, {0xff, 0x45, 0x67, 0xfa, 0x01, 0x2b, 0xcf, 0xff,
                0xff, 0xfc, 0xb2, 0x10, 0xaf, 0x76, 0x54, 0xff}},
    {0xEF, 6, {0x10, 0x0d, 0x04, 0x08, 0x3f, 0x1f}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}},
};

}  // namespace

void FrameBufferGfx::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!_fb || x < 0 || y < 0 || x >= _width || y >= _height) return;
    int16_t t;
    switch (rotation) {
        case 1: t = x; x = WIDTH - 1 - y; y = t; break;
        case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
        case 3: t = x; x = y; y = HEIGHT - 1 - t; break;
        default: break;
    }
    put(static_cast<int32_t>(y) * WIDTH + x, color);
    mark(y);
}

// One pixel of the frame buffer. This panel takes each pixel's three bytes as red, blue,
// green (seen on the Olimex MIPI-LCD2.8 V2: blue drawn as blue-first bytes came out red).
void FrameBufferGfx::put(int32_t i, uint16_t color) {
    const uint8_t r = (color >> 11) & 0x1F, g = (color >> 5) & 0x3F, b = color & 0x1F;
    uint8_t* p = _fb + i * 3;
    p[0] = (r << 3) | (r >> 2);
    p[1] = (b << 3) | (b >> 2);
    p[2] = (g << 2) | (g >> 4);
}

void FrameBufferGfx::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (!_fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;
    if (rotation == 0) {
        for (int16_t r = y; r < y + h; r++) {
            const int32_t row = static_cast<int32_t>(r) * WIDTH + x;
            for (int16_t c = 0; c < w; c++) put(row + c, color);
        }
        mark(y);
        mark(y + h - 1);
        return;
    }
    for (int16_t r = y; r < y + h; r++) {
        for (int16_t c = x; c < x + w; c++) drawPixel(c, r, color);
    }
}

bool FrameBufferGfx::take_dirty(int16_t* y0, int16_t* y1) {
    if (_dirty_y1 <= _dirty_y0) return false;
    *y0 = _dirty_y0;
    *y1 = _dirty_y1;
    _dirty_y0 = INT16_MAX;
    _dirty_y1 = 0;
    return true;
}

St7701::St7701(const char* id, int rotation, int lane_mbps, int dpi_mhz)
    : Display(id), _gfx(kPanelWidth, kPanelHeight),
      _rotation(rotation), _lane_mbps(lane_mbps), _dpi_mhz(dpi_mhz) {}

void St7701::setup() {
    static esp_ldo_channel_handle_t ldo = nullptr;
    esp_ldo_channel_config_t ldo_config = {};
    ldo_config.chan_id = kPhyLdoChannel;
    ldo_config.voltage_mv = kPhyLdoMv;
    esp_err_t err = esp_ldo_acquire_channel(&ldo_config, &ldo);
    if (err != ESP_OK) {
        Serial.printf("ST7701[%s]: **ERROR -- MIPI PHY power: %s\n", id(), esp_err_to_name(err));
        return;
    }

    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t bus_config = {};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = 1;
    bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = _lane_mbps;
    err = esp_lcd_new_dsi_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        Serial.printf("ST7701[%s]: **ERROR -- DSI bus: %s\n", id(), esp_err_to_name(err));
        return;
    }

    // Commands go over the DSI's DBI channel, before the DPI video stream starts.
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_dbi_io_config_t io_config = {};
    io_config.virtual_channel = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(bus, &io_config, &io);
    if (err != ESP_OK) {
        Serial.printf("ST7701[%s]: **ERROR -- DSI command channel: %s\n", id(),
                      esp_err_to_name(err));
        return;
    }
    for (const InitCmd& c : kVendorInit) esp_lcd_panel_io_tx_param(io, c.cmd, c.data, c.len);
    const uint8_t madctl = 0x00;   // the panel's own scan order; rotation is in software
    const uint8_t colmod = 0x77;   // 24 bits per pixel, as the DPI sends it
    esp_lcd_panel_io_tx_param(io, 0x36, &madctl, 1);
    esp_lcd_panel_io_tx_param(io, 0x3A, &colmod, 1);
    esp_lcd_panel_io_tx_param(io, 0x11, nullptr, 0);   // sleep out
    delay(120);
    esp_lcd_panel_io_tx_param(io, 0x29, nullptr, 0);   // display on
    delay(20);

    // RGB888 in the frame buffer and on the link, as Olimex's demo has it.
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = _dpi_mhz;
    dpi.in_color_format = LCD_COLOR_FMT_RGB888;
    dpi.out_color_format = LCD_COLOR_FMT_RGB888;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = kPanelWidth;
    dpi.video_timing.v_size = kPanelHeight;
    dpi.video_timing.hsync_pulse_width = 4;
    dpi.video_timing.hsync_back_porch = 20;
    dpi.video_timing.hsync_front_porch = 10;
    dpi.video_timing.vsync_pulse_width = 4;
    dpi.video_timing.vsync_back_porch = 14;
    dpi.video_timing.vsync_front_porch = 8;
    err = esp_lcd_new_panel_dpi(bus, &dpi, &_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(_panel);
    void* fb = nullptr;
    if (err == ESP_OK) err = esp_lcd_dpi_panel_get_frame_buffer(_panel, 1, &fb);
    if (err != ESP_OK) {
        Serial.printf("ST7701[%s]: **ERROR -- DPI panel: %s\n", id(), esp_err_to_name(err));
        return;
    }
    _gfx.attach(static_cast<uint8_t*>(fb));
    _gfx.setRotation(_rotation);
    _gfx.fillScreen(kBlack);
    do_flush();
    Serial.printf("ST7701[%s]: %dx%d, DSI 1 lane at %d Mbps, DPI %d MHz\n", id(), _gfx.width(),
                  _gfx.height(), _lane_mbps, _dpi_mhz);
}

void St7701::do_text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color,
                     uint16_t bg) {
    _gfx.setTextSize(size);
    _gfx.setTextColor(color, bg);
    _gfx.setTextWrap(false);
    _gfx.setCursor(x, y);
    _gfx.print(s);
}

// The DSI host reads the frame buffer from PSRAM, so what the CPU drew has to be written
// back from its cache first: the changed rows only.
void St7701::do_flush() {
    int16_t y0, y1;
    if (!_gfx.attached() || !_gfx.take_dirty(&y0, &y1)) return;
    uint8_t* start = _gfx.buffer() + static_cast<size_t>(y0) * kPanelWidth * 3;
    const size_t bytes = static_cast<size_t>(y1 - y0) * kPanelWidth * 3;
    esp_cache_msync(start, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}
