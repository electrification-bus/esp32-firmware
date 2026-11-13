#include <peripheral/touch.h>
#include <Arduino.h>

// Samples that must agree before a press or release counts.
#define TOUCH_DEBOUNCE_SAMPLES 2

Touch::Touch(const char* id, const char* display_id, const Calibration& cal, uint16_t sample_ms,
             uint16_t z_threshold)
    : Peripheral(id, kKind),
      _display_id(display_id),
      _cal(cal),
      _sample_ms(sample_ms),
      _z_threshold(z_threshold) {}

void Touch::loop() {
    const uint32_t now = millis();
    if (now - _last_sample_ms < _sample_ms) return;
    _last_sample_ms = now;

    uint16_t rx, ry, rz;
    if (!read_raw(&rx, &ry, &rz)) return;
    const bool down = rz > _z_threshold;

    // A held finger moves silently. Its position is taken one sample late: the reading just
    // before lift-off can pass the pressure threshold with x or y near an edge, so a sample
    // counts only once the next one is pressed too.
    if (down == _state.pressed) {
        _disagreeing = 0;
        if (down) {
            _state.x = _pending_x;
            _state.y = _pending_y;
            map(rx, ry, &_pending_x, &_pending_y);
        }
        return;
    }
    if (++_disagreeing < TOUCH_DEBOUNCE_SAMPLES) return;
    _disagreeing = 0;
    _state.pressed = down;
    _state.since_ms = now;
    if (down) {
        map(rx, ry, &_state.x, &_state.y);
        _pending_x = _state.x;
        _pending_y = _state.y;
    }
    if (_fn) _fn(_ctx, _state);
}

// v from [lo, hi] to [0, 1], clamped.
static float normalize(uint16_t v, uint16_t lo, uint16_t hi) {
    if (hi <= lo) return 0.0f;
    float n = (static_cast<float>(v) - lo) / (hi - lo);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

// Raw -> 0..1 on each axis, through the calibration, into the display's native orientation,
// then rotated as the display is (the Adafruit GFX convention: rotation 1 is a quarter turn
// with x along the native y).
void Touch::map(uint16_t rx, uint16_t ry, int16_t* x, int16_t* y) {
    if (!_display && !_display_missing) {
        _display = peripheral<Display>(_display_id);
        if (!_display) {
            _display_missing = true;
            Serial.printf("TOUCH[%s]: **ERROR -- display '%s' not found; reporting raw values\n",
                          id(), _display_id);
        }
    }
    if (!_display) {
        *x = rx;
        *y = ry;
        return;
    }
    float nx = normalize(rx, _cal.x_min, _cal.x_max);
    float ny = normalize(ry, _cal.y_min, _cal.y_max);
    if (_cal.swap_xy) {
        const float t = nx;
        nx = ny;
        ny = t;
    }
    if (_cal.invert_x) nx = 1.0f - nx;
    if (_cal.invert_y) ny = 1.0f - ny;

    const uint8_t rot = _display->rotation() & 3;
    const int16_t w = _display->width(), h = _display->height();
    const int16_t w0 = (rot & 1) ? h : w;   // native size
    const int16_t h0 = (rot & 1) ? w : h;
    const int16_t px = static_cast<int16_t>(nx * (w0 - 1) + 0.5f);
    const int16_t py = static_cast<int16_t>(ny * (h0 - 1) + 0.5f);
    switch (rot) {
        case 0: *x = px;           *y = py;           break;
        case 1: *x = py;           *y = w0 - 1 - px;  break;
        case 2: *x = w0 - 1 - px;  *y = h0 - 1 - py;  break;
        default: *x = h0 - 1 - py; *y = px;           break;
    }
}
