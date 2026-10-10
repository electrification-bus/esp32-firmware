#include <i2c/gme12864.h>
#include <homie/Property.h>   // entity_settable_callback(Property*) dereferences it
#include <util/utf8.h>

static const uint8_t DISPLAY_WIDTH  = 128;
static const uint8_t DISPLAY_HEIGHT = 64;

// text() grid, indexed by font_size - 1. Heights are 8 * font_size, i.e. whole SSD1306 pages.
static const uint8_t* const LINE_FONTS[GME12864::MAX_FONT_SIZE] = {
    u8g2_font_5x7_tf,
    u8g2_font_9x15_tf,
    u8g2_font_logisoso24_tf,
};

// Full-screen layouts shrink to fit: a layout starts at its preferred font and steps
// down this ladder, largest first, until all of the text fits in its lines. Fonts taller
// than a layout's line band are skipped on the way, so the ladder serves every layout.
static const uint8_t* const FIT_FONTS[] = {
    u8g2_font_logisoso32_tf,   // ~6 chars across 128 px
    u8g2_font_logisoso24_tf,   // ~9
    u8g2_font_logisoso20_tf,   // ~10
    u8g2_font_10x20_tf,        // 12
    u8g2_font_9x15_tf,         // 14
    u8g2_font_6x10_tf,         // 21
    u8g2_font_5x7_tf,          // 25
};
static const uint8_t FIT_FONT_COUNT = sizeof(FIT_FONTS) / sizeof(FIT_FONTS[0]);

// Where each layout starts on the ladder, indexed by num_lines - 1 — the font it used
// before shrink-to-fit, so text that already fitted looks exactly as it did.
static const uint8_t LAYOUT_START[GME12864::MAX_LAYOUT_LINES] = { 0, 1, 3 };

static const char* const LAYOUT_IDS[GME12864::MAX_LAYOUT_LINES] = {
    "one-line", "two-line", "three-line",
};

// The per-line properties, in grid order: LINE_IDS[i] drives line i.
static const char* const LINE_IDS[GME12864::MAX_LAYOUT_LINES] = {
    "line-one", "line-two", "line-three",
};

// Longest single display line kept by the splitter. Even the narrowest layout font fits
// ~12 characters across 128 px, so this only bounds the scratch buffer.
#define GME12864_LINE_MAX 48
static_assert(GME12864_LINE_MAX == GME12864::LINE_TEXT_MAX,
              "the splitter scratch and the remembered line text must be the same width");

bool GME12864::setup() {
    I2CEntity::setup();  // Base class first: NodeEntity, then the shared bus

    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        _layouts[i]->setup(getNodeId(), LAYOUT_IDS[i],
                           &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    }
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        _lines_props[i]->setup(getNodeId(), LINE_IDS[i],
                               &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    }
    // Before the probe: with no display, empty is still true. Set here, published by the
    // boot publishTree() that follows register_homie_properties().
    blankLayouts(nullptr);
    blankLines(nullptr);

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
    blankLines(nullptr);
}

void GME12864::showDefault() {
    if (!_present) return;
    _u8g2.clearBuffer();
    drawCentered("eBus", u8g2_font_logisoso24_tf, 10);
    drawCentered(getNodeId(), u8g2_font_5x7_tf, 46);
    _u8g2.sendBuffer();
    _showing_default = true;
    blankLayouts(nullptr);
    blankLines(nullptr);
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
    _u8g2.drawUTF8(0, y, text);

    if (whole_screen) {
        _u8g2.sendBuffer();
    } else {
        // Push only the pages this line touches. Arguments are in 8x8 tiles.
        _u8g2.updateDisplayArea(0, y / 8, DISPLAY_WIDTH / 8, height / 8);
    }
    blankLayouts(nullptr);
    blankLines(nullptr);
    return true;
}

// Split `text` into at most `max_lines` display lines for the current font. An explicit
// '|' or newline anywhere means the caller chose the breaks; otherwise words are packed
// greedily into the screen width. A single word wider than the screen gets a line to
// itself and is clipped when drawn. Returns the number of lines produced; `*complete`
// says whether ALL of the text was placed and every line is within the screen width —
// what showLines() uses to decide whether to try a smaller font.
static uint8_t split_lines(U8G2& u8g2, const char* text, uint8_t max_lines,
                           char out[][GME12864_LINE_MAX], bool* complete) {
    uint8_t n = 0;
    *complete = true;
    for (uint8_t i = 0; i < max_lines; i++) out[i][0] = '\0';

    if (strpbrk(text, "|\n")) {
        const char* p = text;
        while (true) {
            if (n == max_lines) { *complete = false; break; }   // more segments than lines
            size_t len = strcspn(p, "|\n");
            if (len >= GME12864_LINE_MAX) {
                len = utf8_fit(p, len, GME12864_LINE_MAX - 1);
                *complete = false;
            }
            memcpy(out[n], p, len);
            out[n][len] = '\0';
            if (u8g2.getUTF8Width(out[n]) > DISPLAY_WIDTH) *complete = false;
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
                word_len = utf8_fit(p, word_len, GME12864_LINE_MAX - 1);   // one oversized word: truncate it
                need = word_len;
                *complete = false;
            }
            char candidate[GME12864_LINE_MAX];
            memcpy(candidate, line, used);
            size_t c = used;
            if (used) candidate[c++] = ' ';
            memcpy(candidate + c, p, word_len);
            candidate[need] = '\0';
            if (used && u8g2.getUTF8Width(candidate) > DISPLAY_WIDTH) break;
            memcpy(line, candidate, need + 1);
            used = need;
            p = word_end;
            while (*p == ' ') p++;
        }
        if (u8g2.getUTF8Width(line) > DISPLAY_WIDTH) *complete = false;   // lone wide word
        n++;
    }
    while (*p == ' ') p++;
    if (*p) *complete = false;   // ran out of lines with text left over
    return n;
}

bool GME12864::showLines(const char* text, uint8_t num_lines) {
    if (!_present || !text) return false;
    if (num_lines < 1 || num_lines > MAX_LAYOUT_LINES) return false;

    // Each line gets an equal band and is centred in it, both ways.
    uint8_t band = DISPLAY_HEIGHT / num_lines;

    // Largest font, from this layout's preferred one down, in which all of the text fits.
    // If even the smallest cannot hold it, the smallest is used and the excess is clipped
    // or dropped, as before.
    char split[MAX_LAYOUT_LINES][GME12864_LINE_MAX];
    uint8_t n = 0;
    for (uint8_t f = LAYOUT_START[num_lines - 1]; f < FIT_FONT_COUNT; f++) {
        _u8g2.setFont(FIT_FONTS[f]);
        if (_u8g2.getAscent() - _u8g2.getDescent() > band) continue;   // too tall for a line
        bool complete = false;
        n = split_lines(_u8g2, text, num_lines, split, &complete);
        if (complete) break;
    }
    int glyph_h = _u8g2.getAscent() - _u8g2.getDescent();
    _u8g2.clearBuffer();
    for (uint8_t i = 0; i < n; i++) {
        int w = _u8g2.getUTF8Width(split[i]);
        int x = w < DISPLAY_WIDTH ? (DISPLAY_WIDTH - w) / 2 : 0;
        int y = i * band + (band - glyph_h) / 2;
        _u8g2.drawUTF8(x, y < 0 ? 0 : y, split[i]);
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

// The same for the line-* properties, and their remembered text with them: once another
// layout owns the screen those lines are not showing, so a later single-line set must not
// redraw stale companions alongside the one line it was given.
void GME12864::blankLines(NodeProperty* keep) {
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        if (_lines_props[i] == keep) continue;
        _line_text[i][0] = '\0';
        _lines_props[i]->set_value_and_publish("");
    }
}

// Draw all three remembered lines. They go through the three-line layout engine as one
// '|'-separated string: split_lines() treats '|' as a caller-chosen break, so the lines
// land one per row and inherit that layout's shrink-to-fit and centring. Values were
// stripped of '|' on the way in, so the segment count is always MAX_LAYOUT_LINES.
bool GME12864::renderLines() {
    char joined[MAX_LAYOUT_LINES * LINE_TEXT_MAX + MAX_LAYOUT_LINES];
    size_t w = 0;
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        if (i) joined[w++] = '|';
        w += snprintf(joined + w, sizeof(joined) - w, "%s", _line_text[i]);
    }
    joined[w] = '\0';
    return showLines(joined, MAX_LAYOUT_LINES);
}

bool GME12864::setLine(const char* text, uint8_t index) {
    if (!_present || !text || index >= MAX_LAYOUT_LINES) return false;
    // One line by definition: a '|' or newline in the value would otherwise split it into
    // further rows and push the lines below it off the grid.
    char* dst = _line_text[index];
    size_t n = utf8_fit(text, strlen(text), LINE_TEXT_MAX - 1);
    for (size_t i = 0; i < n; i++) {
        char c = text[i];
        dst[i] = (c == '|' || c == '\n' || c == '\r') ? ' ' : c;
    }
    dst[n] = '\0';
    return renderLines();
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
        blankLines(nullptr);   // a whole-screen layout owns the screen now
        return true;
    }
    for (uint8_t i = 0; i < MAX_LAYOUT_LINES; i++) {
        if (strcmp(property_id, _lines_props[i]->homie_property_id()) != 0) continue;
        if (!setLine(value, i)) {
            // Not drawn, so not published: the topic must not claim the screen says it.
            Serial.printf("GME12864[%s]: %s ignored — display not present\n",
                          getNodeId(), property_id);
            return false;
        }
        Serial.printf("GME12864[%s]: %s = '%s'\n", getNodeId(), property_id, value);
        // The stored text, not `value`: a '|' or newline was replaced on the way in, and
        // the retained topic has to say what the screen actually shows.
        _lines_props[i]->set_value_and_publish(_line_text[i]);
        blankLayouts(nullptr);          // no whole-screen layout is showing any more
        return true;
    }
    Serial.printf("GME12864[%s]: unhandled settable '%s'\n", getNodeId(), property_id);
    return false;
}
