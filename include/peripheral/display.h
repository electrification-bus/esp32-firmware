#pragma once

#include <peripheral/peripheral.h>

// The drawing interface every display driver implements over its own graphics library, so
// code that finds a display by id draws on any panel. A driver also offers native(), its
// library's own object, for anything beyond this interface.
//
// Colors are RGB565; a monochrome panel lights any nonzero color. Text uses the library's
// built-in font: size 1 is its base size, larger sizes multiply it. Drawing may be buffered
// until flush().
class Display : public Peripheral {
public:
    static constexpr PeripheralKind kKind = PeripheralKind::kDisplay;

    static constexpr uint16_t kBlack  = 0x0000;
    static constexpr uint16_t kWhite  = 0xFFFF;
    static constexpr uint16_t kRed    = 0xF800;
    static constexpr uint16_t kGreen  = 0x07E0;
    static constexpr uint16_t kBlue   = 0x001F;
    static constexpr uint16_t kYellow = 0xFFE0;
    static constexpr uint16_t kGrey   = 0x7BEF;

    explicit Display(const char* id) : Peripheral(id, kKind) {}

    int16_t width() { PeripheralLock l(*this); return do_width(); }
    int16_t height() { PeripheralLock l(*this); return do_height(); }
    // 0-3, quarter turns from the panel's native orientation; width() and height() follow it.
    uint8_t rotation() { PeripheralLock l(*this); return do_rotation(); }
    void clear(uint16_t color) { PeripheralLock l(*this); do_clear(color); }
    void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        PeripheralLock l(*this);
        do_fill_rect(x, y, w, h, color);
    }
    // Text with its top-left corner at x,y, on a bg-colored box.
    void text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color, uint16_t bg) {
        PeripheralLock l(*this);
        do_text(x, y, s, size, color, bg);
    }
    void flush() { PeripheralLock l(*this); do_flush(); }

protected:
    ~Display() = default;

    virtual int16_t do_width() = 0;
    virtual int16_t do_height() = 0;
    virtual uint8_t do_rotation() { return 0; }
    virtual void do_clear(uint16_t color) = 0;
    virtual void do_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) = 0;
    virtual void do_text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t color,
                         uint16_t bg) = 0;
    virtual void do_flush() = 0;
};
