#include <i2c/gme12864.h>
#include <homie/Property.h>   // entity_settable_callback(Property*) dereferences it

static const uint8_t DISPLAY_WIDTH  = 128;
static const uint8_t DISPLAY_HEIGHT = 64;

// text() grid, indexed by font_size - 1. Heights are 8 * font_size, i.e. whole SSD1306 pages.
static const uint8_t* const LINE_FONTS[GME12864::MAX_FONT_SIZE] = {
    u8g2_font_5x7_tf,
    u8g2_font_9x15_tf,
    u8g2_font_logisoso24_tf,
};

// Full-screen layouts, indexed by num_lines - 1.
static const uint8_t* const LAYOUT_FONTS[GME12864::MAX_LAYOUT_LINES] = {
    u8g2_font_logisoso32_tf,
    u8g2_font_logisoso24_tf,
    u8g2_font_10x20_tf,
};

static const char* const LAYOUT_IDS[GME12864::MAX_LAYOUT_LINES] = {
    "one-line", "two-line", "three-line",
};

// Longest single display line kept by the splitter. Even the narrowest layout font fits
// ~12 characters across 128 px, so this only bounds the scratch buffer.
#define GME12864_LINE_MAX 48

bool GME12864::setup() {
    I2CEntity::setup();  // Base class first: NodeEntity, then the shared bus

    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        _layouts[i]->setup(getNodeId(), LAYOUT_IDS[i],
                           &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    }
    // Before the probe: with no display, empty is still true. Set here, published by the
    // boot publishTree() that follows register_homie_properties().
    blankLayouts(nullptr);

    // U8g2 cannot tell whether anything answered; probe first, so a missing panel is a
    // log line rather than a display object silently writing into the void.
    wire().beginTransmission(address());
    _present = (wire().endTransmission() == 0);
    if (!_present) {
        Serial.printf("GME12864: not found at 0x%02X — display disabled\n", address());
        return false;
    }

    // U8g2 takes the 8-bit (shifted) address. Its begin() re-calls Wire.begin() with the
    // same pins, which the already-started shared bus ignores, and sets the bus to the
    // SSD1306's 400 kHz — fine for every other i2c/* part in the tree.
    _u8g2.setI2CAddress(address() << 1);
    _u8g2.begin();
    _u8g2.setFontPosTop();
    showDefault();
    Serial.printf("GME12864: ready at 0x%02X\n", address());
    return true;
}

uint8_t GME12864::lines(uint8_t font_size) const {
    if (font_size < 1 || font_size > MAX_FONT_SIZE) return 0;
    return DISPLAY_HEIGHT / (8 * font_size);
}

void GME12864::clear() {
    if (!_present) return;
    _u8g2.clearBuffer();
    _u8g2.sendBuffer();
    _showing_default = false;
    blankLayouts(nullptr);
}

void GME12864::showDefault() {
    if (!_present) return;
    _u8g2.clearBuffer();
    drawCentered("eBus", u8g2_font_logisoso24_tf, 10);
    drawCentered(getNodeId(), u8g2_font_5x7_tf, 46);
    _u8g2.sendBuffer();
    _showing_default = true;
    blankLayouts(nullptr);
}

// One line of the splash, centered across the screen (left-aligned and clipped if wider).
void GME12864::drawCentered(const char* text, const uint8_t* font, uint8_t y) {
    _u8g2.setFont(font);
    int w = _u8g2.getStrWidth(text);
    _u8g2.drawStr(w < DISPLAY_WIDTH ? (DISPLAY_WIDTH - w) / 2 : 0, y, text);
}

bool GME12864::text(const char* text, uint8_t line_num, uint8_t font_size) {
    if (!_present || !text) return false;
    if (line_num >= lines(font_size)) return false;   // also rejects a bad font_size

    uint8_t height = 8 * font_size;
    uint8_t y      = line_num * height;

    // The first text replaces the whole default screen, not just one band of it.
    bool whole_screen = _showing_default;
    if (whole_screen) {
        _u8g2.clearBuffer();
        _showing_default = false;
    } else {
        _u8g2.setDrawColor(0);
        _u8g2.drawBox(0, y, DISPLAY_WIDTH, height);
        _u8g2.setDrawColor(1);
    }

    _u8g2.setFont(LINE_FONTS[font_size - 1]);
    _u8g2.drawStr(0, y, text);

    if (whole_screen) {
        _u8g2.sendBuffer();
    } else {
        // Push only the pages this line touches. Arguments are in 8x8 tiles.
        _u8g2.updateDisplayArea(0, y / 8, DISPLAY_WIDTH / 8, height / 8);
    }
    blankLayouts(nullptr);
    return true;
}

// Split `text` into at most `max_lines` display lines for the current font. An explicit
// '|' or newline anywhere means the caller chose the breaks; otherwise words are packed
// greedily into the screen width. A single word wider than the screen gets a line to
// itself and is clipped when drawn. Returns the number of lines produced.
static uint8_t split_lines(U8G2& u8g2, const char* text, uint8_t max_lines,
                           char out[][GME12864_LINE_MAX]) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < max_lines; i++) out[i][0] = '\0';

    if (strpbrk(text, "|\n")) {
        const char* p = text;
        while (n < max_lines) {
            size_t len = strcspn(p, "|\n");
            if (len >= GME12864_LINE_MAX) len = GME12864_LINE_MAX - 1;
            memcpy(out[n], p, len);
            out[n][len] = '\0';
            n++;
            p += strcspn(p, "|\n");
            if (*p == '\0') break;
            p++;   // past the separator
        }
        return n;
    }

    const char* p = text;
    while (*p && n < max_lines) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* line = out[n];
        size_t used = 0;
        while (*p) {
            const char* word_end = p;
            while (*word_end && *word_end != ' ') word_end++;
            size_t word_len = word_end - p;
            size_t need = used + (used ? 1 : 0) + word_len;
            if (need >= GME12864_LINE_MAX) {
                if (used) break;
                word_len = GME12864_LINE_MAX - 1;   // one oversized word: truncate it
                need = word_len;
            }
            char candidate[GME12864_LINE_MAX];
            memcpy(candidate, line, used);
            size_t c = used;
            if (used) candidate[c++] = ' ';
            memcpy(candidate + c, p, word_len);
            candidate[need] = '\0';
            if (used && u8g2.getStrWidth(candidate) > DISPLAY_WIDTH) break;
            memcpy(line, candidate, need + 1);
            used = need;
            p = word_end;
            while (*p == ' ') p++;
        }
        n++;
    }
    return n;
}

bool GME12864::showLines(const char* text, uint8_t num_lines) {
    if (!_present || !text) return false;
    if (num_lines < 1 || num_lines > MAX_LAYOUT_LINES) return false;

    _u8g2.setFont(LAYOUT_FONTS[num_lines - 1]);
    char split[MAX_LAYOUT_LINES][GME12864_LINE_MAX];
    uint8_t n = split_lines(_u8g2, text, num_lines, split);

    // Each line gets an equal band and is centred in it, both ways.
    uint8_t band   = DISPLAY_HEIGHT / num_lines;
    int     glyph_h = _u8g2.getAscent() - _u8g2.getDescent();
    _u8g2.clearBuffer();
    for (uint8_t i = 0; i < n; i++) {
        int w = _u8g2.getStrWidth(split[i]);
        int x = w < DISPLAY_WIDTH ? (DISPLAY_WIDTH - w) / 2 : 0;
        int y = i * band + (band - glyph_h) / 2;
        _u8g2.drawStr(x, y < 0 ? 0 : y, split[i]);
    }
    _u8g2.sendBuffer();
    _showing_default = false;
    return true;
}

// Every layout but `keep` goes to the empty string. The retained-property publish gate
// drops a repeat of the same payload, so this costs nothing for one already empty.
void GME12864::blankLayouts(NodeProperty* keep) {
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        if (_layouts[i] != keep) _layouts[i]->set_value_and_publish("");
    }
}

bool GME12864::entity_settable_callback(Property* property) {
    if (!property) return false;
    return entity_settable_callback(property->id(), property->value());
}

bool GME12864::entity_settable_callback(const char* property_id, const char* value) {
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        // The id setup() stored is the one /set dispatch hands us — already Homie-legal,
        // so it equals LAYOUT_IDS[i], but comparing against it cannot drift if it isn't.
        if (strcmp(property_id, _layouts[i]->homie_property_id()) != 0) continue;
        if (!showLines(value, i + 1)) {
            // Not drawn, so not published: the topic must not claim the screen says it.
            Serial.printf("GME12864[%s]: %s ignored — display not present\n",
                          getNodeId(), property_id);
            return false;
        }
        Serial.printf("GME12864[%s]: %s = '%s'\n", getNodeId(), property_id, value);
        _layouts[i]->set_value_and_publish(value);
        blankLayouts(_layouts[i]);
        return true;
    }
    Serial.printf("GME12864[%s]: unhandled settable '%s'\n", getNodeId(), property_id);
    return false;
}
