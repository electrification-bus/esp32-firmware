#pragma once

#include <peripheral/display.h>

// A touch panel over a display. The driver's loop() samples it every sample_ms, debounces
// presses and releases (two agreeing samples), and maps raw readings to the display's
// pixels in its current rotation. App code reads state() at any time, from any task, without
// touching the bus, and may register a callback for press and release; the callback runs on
// the loop task, from loop_peripherals(), with the touch peripheral locked.
struct TouchState {
    bool pressed;
    int16_t x;           // pixels in the display's current orientation; the last position
    int16_t y;           //   while pressed, the release position after
    uint32_t since_ms;   // millis() of the last press or release
};

typedef void (*TouchFn)(void* ctx, const TouchState& state);

class Touch : public Peripheral {
public:
    static constexpr PeripheralKind kKind = PeripheralKind::kTouch;

    // Raw readings at the panel's edges, and how its axes sit relative to the display's
    // native (rotation 0) orientation.
    struct Calibration {
        uint16_t x_min, x_max, y_min, y_max;
        bool swap_xy, invert_x, invert_y;
    };

    Touch(const char* id, const char* display_id, const Calibration& cal, uint16_t sample_ms,
          uint16_t z_threshold);

    TouchState state() { PeripheralLock l(*this); return _state; }
    void on_touch(TouchFn fn, void* ctx) {
        PeripheralLock l(*this);
        _fn = fn;
        _ctx = ctx;
    }

    void loop() override;

protected:
    ~Touch() = default;

    // One raw sample, each 0-4095; false when the read failed or is not usable.
    virtual bool read_raw(uint16_t* x, uint16_t* y, uint16_t* z) = 0;

    uint16_t z_threshold() const { return _z_threshold; }

private:
    void map(uint16_t rx, uint16_t ry, int16_t* x, int16_t* y);

    const char* _display_id;
    Display* _display = nullptr;
    bool _display_missing = false;
    Calibration _cal;
    uint16_t _sample_ms;
    uint16_t _z_threshold;
    uint32_t _last_sample_ms = 0;
    uint8_t _disagreeing = 0;   // consecutive samples that differ from _state.pressed
    TouchState _state = {false, 0, 0, 0};
    int16_t _pending_x = 0, _pending_y = 0;   // the latest held position, not yet confirmed
    TouchFn _fn = nullptr;
    void* _ctx = nullptr;
};
