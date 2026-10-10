#pragma once
#include <U8g2lib.h>
#include <i2c/i2c_entity.h>

// GME12864 — the 0.96" 128x64 monochrome OLED module, an SSD1306 on I2C (usually 0x3C).
// Not the 1.3" SH1106 panels: same resolution, different controller.
//
// Two ways to put text on it:
//
// 1. Over Homie — three settable string properties, each taking just the text and filling
//    the whole screen in that many lines, at the largest font the text fits:
//
//      one-line    1 line   starts at logisoso 32 (~6 chars), shrinks to 5x7 (25 chars)
//      two-line    2 lines  starts at logisoso 24 (~9 chars each), shrinks to 5x7
//      three-line  3 lines  starts at 10x20 (12 chars each), shrinks to 5x7
//
//    The text is split at '|' or a newline if it has one ("Pump|ON"), otherwise
//    word-wrapped. If it does not all fit, the next smaller font is tried, so short text
//    stays big and longer text shrinks rather than losing words. Only text too long even
//    at 5x7 is clipped or dropped. Each line is centred.
//    The properties are retained and always say what is on screen: the layout showing
//    holds its text, the other two hold the empty string. All three start empty at boot
//    (the splash is showing), which also overwrites whatever a previous run left on the
//    broker. Empty is a real value here — "this layout is not showing" — not a sentinel
//    for a failed reading; it goes out as Homie's single 0x00 byte, not a retraction.
//
// 1b. Over Homie, one line at a time — three more settable string properties that address
//    the lines of the three-line grid individually:
//
//      line-one    line 1      line-two    line 2      line-three   line 3
//
//    Setting one replaces ONLY that line and leaves the other two as they are, so a
//    caller that owns one line need not know or resend the others. Showing "one"/"two"/
//    "three" and then setting line-one to "foo" leaves "foo"/"two"/"three".
//
//    The three are rendered together through the same layout engine as three-line, so
//    they share its shrink-to-fit and centring: the font is the largest in which all
//    three fit. A '|' or newline inside a value is replaced with a space — the value is
//    one line by definition, and the separator would otherwise split it into more.
//
//    The two groups are alternatives, and the retained values say which is on screen.
//    Setting a line-* clears all three layout properties; setting a layout (or calling
//    text(), clear() or showDefault()) clears all three line-* properties and forgets
//    their text, so nothing claims to be showing what the screen has moved on from.
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
// Until the first write the display shows the default screen: a splash of "eBus" over
// the node id.
//
// Text is UTF-8 and drawn with U8g2's UTF-8 calls; the fonts cover Latin-1, so "°C" and
// accented letters render.
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

        // Replace line `index` (0-based) of the three-line grid, keeping the other lines,
        // and redraw all three — what the line-one / line-two / line-three properties
        // call. `text` may be empty to blank just that line. Does not publish.
        bool setLine(const char* text, uint8_t index);

        void clear();          // blank the whole screen
        void showDefault();    // back to the splash screen

        bool present() const { return _present; }

        // Both overloads, because which one fires depends on whether MQTT was connected
        // when the properties subscribed (see CLAUDE.md / Property::subscribe()).
        bool entity_settable_callback(Property* property) override;
        bool entity_settable_callback(const char* property_id, const char* value) override;

    public:
        // Longest text one display line keeps. Even the narrowest layout font fits ~25
        // characters across 128 px, so this only bounds the buffers.
        static const uint8_t LINE_TEXT_MAX = 48;

    private:
        void blankLayouts(NodeProperty* keep);
        // Every line-* property to the empty string and its remembered text forgotten,
        // for when something else takes the screen. `keep` is spared.
        void blankLines(NodeProperty* keep);
        // Draw the three remembered lines, joined for the three-line layout engine.
        bool renderLines();
        void drawCentered(const char* text, const uint8_t* font, uint8_t y);

        U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;
        bool _present         = false;
        bool _showing_default = false;

        // The one-line / two-line / three-line properties; indexed by _layouts[num_lines - 1].
        NodeProperty one_line;
        NodeProperty two_line;
        NodeProperty three_line;
        NodeProperty* const _layouts[MAX_LAYOUT_LINES] = { &one_line, &two_line, &three_line };

        // The line-one / line-two / line-three properties and the text each is showing.
        // _line_text is the state that makes a single-line set preserve the others.
        NodeProperty line_one;
        NodeProperty line_two;
        NodeProperty line_three;
        NodeProperty* const _lines_props[MAX_LAYOUT_LINES] = { &line_one, &line_two, &line_three };
        char _line_text[MAX_LAYOUT_LINES][LINE_TEXT_MAX] = {{0}};
};
