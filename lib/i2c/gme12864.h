#pragma once
#include <U8g2lib.h>
#include <i2c/i2c_entity.h>

// GME12864 — the 0.96" 128x64 monochrome OLED module, an SSD1306 on I2C (usually 0x3C).
// Not the 1.3" SH1106 panels: same resolution, different controller.
//
// Two ways to put text on it:
//
// 1. Over Homie — three settable string properties, each taking just the text and filling
//    the whole screen at a font sized for that many lines:
//
//      one-line    1 line,  logisoso 32   ~6 chars
//      two-line    2 lines, logisoso 24   ~9 chars each
//      three-line  3 lines, 10x20         12 chars each
//
//    The text is split at '|' or a newline if it has one ("Pump|ON"), otherwise
//    word-wrapped; anything past the last line is dropped. Each line is centred.
//    The properties are retained and always say what is on screen: the layout showing
//    holds its text, the other two hold the empty string. All three start empty at boot
//    (the screen is on Rick), which also overwrites whatever a previous run left on the
//    broker. Empty is a real value here — "this layout is not showing" — not a sentinel
//    for a failed reading; it goes out as Homie's single 0x00 byte, not a retraction.
//
// 2. From firmware — text(text, line_num, font_size) writes one line of a grid whose line
//    height depends on the font size:
//
//      font_size  line height  lines  font         ~chars/line
//          1          8 px       8    5x7               25
//          2         16 px       4    9x15              14
//          3         32 px       2    logisoso 24       ~9
//
//    Line heights are multiples of the controller's 8-pixel pages, so the grids nest:
//    size-2 line n covers size-1 lines 2n and 2n+1. Writing a line erases only its own
//    band. Because the screen then no longer shows any layout, text() — like clear()
//    and showDefault() — sets all three layout properties back to empty.
//
// Until the first write the display shows the default screen, the Rick graphic.
//
// U8g2's full-frame buffer (1 KB) is a static array inside the library, not heap.
class GME12864 : public I2CEntity {
    public:
        static const uint8_t MAX_FONT_SIZE = 3;
        static const uint8_t MAX_LAYOUT_LINES = 3;

        GME12864(const char* homie_node_id, int pin_sda, int pin_scl, uint8_t address)
            : I2CEntity(homie_node_id, pin_sda, pin_scl, address),
              _u8g2(U8G2_R0, U8X8_PIN_NONE, pin_scl, pin_sda) {}
        ~GME12864(){};

        bool setup() override;
        void loop() override {}

        // Write `text` on line `line_num` (0-based) in font size 1..MAX_FONT_SIZE,
        // replacing whatever was in that line's band. Text wider than the screen is
        // clipped. Returns false — and draws nothing — if the display is absent or the
        // line/size is out of range.
        bool text(const char* text, uint8_t line_num, uint8_t font_size = 1);

        // How many text() lines fit at `font_size` (0 for an invalid size).
        uint8_t lines(uint8_t font_size) const;

        // Fill the whole screen with `text` laid out on 1..MAX_LAYOUT_LINES lines — what
        // the one-line / two-line / three-line properties call. Does not publish.
        bool showLines(const char* text, uint8_t num_lines);

        void clear();          // blank the whole screen
        void showDefault();    // back to the Rick screen

        bool present() const { return _present; }

        // Both overloads, because which one fires depends on whether MQTT was connected
        // when the properties subscribed (see CLAUDE.md / Property::subscribe()).
        void entity_settable_callback(Property* property) override;
        void entity_settable_callback(const char* property_id, const char* value) override;

    private:
        void blankLayouts(NodeProperty* keep);

        U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;
        bool _present         = false;
        bool _showing_default = false;

        // The one-line / two-line / three-line properties; indexed by _layouts[num_lines - 1].
        NodeProperty one_line;
        NodeProperty two_line;
        NodeProperty three_line;
        NodeProperty* const _layouts[MAX_LAYOUT_LINES] = { &one_line, &two_line, &three_line };
};
