#include <homie/homie_datatype.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

// --- helpers ------------------------------------------------------------------

// Strict float lexer over [begin,end): optional sign, digits, one optional '.', more
// digits; at least one digit overall. No exponent (Homie float format is plain). On
// success writes the value and returns true. Rejects spaces and stray characters —
// the caller has already split on ',' so a token must be a bare number.
static bool parse_float_token(const char* begin, const char* end, float* out) {
    if (begin >= end) return false;
    const char* p = begin;
    bool any_digit = false, dot = false;
    if (*p == '+' || *p == '-') p++;
    for (; p < end; ++p) {
        if (*p >= '0' && *p <= '9') { any_digit = true; }
        else if (*p == '.' && !dot) { dot = true; }
        else return false;
    }
    if (!any_digit) return false;
    char buf[40];
    size_t n = (size_t)(end - begin);
    if (n >= sizeof(buf)) return false;
    memcpy(buf, begin, n);
    buf[n] = '\0';
    *out = (float)atof(buf);
    return true;
}

// True if `needle` (length nlen) appears as a whole comma-separated entry in `list`.
static bool csv_contains(const char* list, const char* needle, size_t nlen) {
    if (!list) return false;
    const char* p = list;
    while (*p) {
        const char* comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == nlen && strncmp(p, needle, nlen) == 0) return true;
        if (!comma) break;
        p = comma + 1;
    }
    return false;
}

// --- enum ---------------------------------------------------------------------

bool homie_validate_enum(const char* payload, const char* format) {
    if (!payload || payload[0] == '\0') return false;   // empty string invalid
    if (!format || format[0] == '\0') return false;     // enum requires a format
    return csv_contains(format, payload, strlen(payload));
}

// --- color --------------------------------------------------------------------

bool homie_parse_color(const char* payload, const char* format, HomieColor* out) {
    if (!payload || payload[0] == '\0') return false;
    if (strchr(payload, ' ')) return false;             // no spaces permitted

    // First comma-separated token is the type.
    const char* first_comma = strchr(payload, ',');
    if (!first_comma) return false;
    size_t tlen = (size_t)(first_comma - payload);
    int type;
    if (tlen == 3 && strncmp(payload, "rgb", 3) == 0) type = 0;
    else if (tlen == 3 && strncmp(payload, "hsv", 3) == 0) type = 1;
    else if (tlen == 3 && strncmp(payload, "xyz", 3) == 0) type = 2;
    else return false;

    // If a format is supplied, the type must be one the property advertises.
    if (format && format[0] != '\0' && !csv_contains(format, payload, tlen)) return false;

    int want = (type == 2) ? 2 : 3;                     // xyz has 2 numbers, rgb/hsv have 3
    float vals[3];
    const char* p = first_comma + 1;
    for (int i = 0; i < want; ++i) {
        const char* comma = strchr(p, ',');
        const char* end = comma ? comma : p + strlen(p);
        if (!parse_float_token(p, end, &vals[i])) return false;
        if (i < want - 1) {
            if (!comma) return false;                   // missing a component
            p = comma + 1;
        } else {
            if (comma) return false;                    // trailing extra component
        }
    }

    // Per-type inclusive range checks.
    bool ok = false;
    if (type == 0) {        // rgb 0..255
        ok = vals[0] >= 0 && vals[0] <= 255 && vals[1] >= 0 && vals[1] <= 255 &&
             vals[2] >= 0 && vals[2] <= 255;
    } else if (type == 1) { // hsv: h 0..360, s/v 0..100
        ok = vals[0] >= 0 && vals[0] <= 360 && vals[1] >= 0 && vals[1] <= 100 &&
             vals[2] >= 0 && vals[2] <= 100;
    } else {                // xyz 0..1
        ok = vals[0] >= 0 && vals[0] <= 1 && vals[1] >= 0 && vals[1] <= 1;
    }
    if (!ok) return false;
    if (out) {
        out->type = type;
        out->c[0] = vals[0];
        out->c[1] = vals[1];
        out->c[2] = (want == 3) ? vals[2] : 0.0f;
    }
    return true;
}

bool homie_validate_color(const char* payload, const char* format) {
    return homie_parse_color(payload, format, nullptr);
}

// --- datetime -----------------------------------------------------------------

static bool digits(const char* s, int n) {
    for (int i = 0; i < n; ++i) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

bool homie_validate_datetime(const char* payload) {
    if (!payload) return false;
    const char* p = payload;
    // Calendar date: YYYY-MM-DD (required).
    if (!digits(p, 4) || p[4] != '-' || !digits(p + 5, 2) || p[7] != '-' || !digits(p + 8, 2))
        return false;
    int month = (p[5] - '0') * 10 + (p[6] - '0');
    int day   = (p[8] - '0') * 10 + (p[9] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    p += 10;
    if (*p == '\0') return true;                        // date-only is acceptable

    // Time: 'T' hh:mm[:ss][.fff]
    if (*p != 'T' && *p != ' ') return false;
    p++;
    if (!digits(p, 2) || p[2] != ':' || !digits(p + 3, 2)) return false;
    int hh = (p[0] - '0') * 10 + (p[1] - '0');
    int mm = (p[3] - '0') * 10 + (p[4] - '0');
    if (hh > 23 || mm > 59) return false;
    p += 5;
    if (*p == ':') {                                    // optional seconds
        if (!digits(p + 1, 2)) return false;
        int ss = (p[1] - '0') * 10 + (p[2] - '0');
        if (ss > 60) return false;                      // 60 allows a leap second
        p += 3;
        if (*p == '.') {                                // optional fractional seconds
            p++;
            if (!isdigit((unsigned char)*p)) return false;
            while (isdigit((unsigned char)*p)) p++;
        }
    }
    if (*p == '\0') return true;                         // no zone (local time)
    if (*p == 'Z') return *(p + 1) == '\0';             // UTC
    if (*p == '+' || *p == '-') {                       // ±hh[:mm] offset
        p++;
        if (!digits(p, 2)) return false;
        p += 2;
        if (*p == '\0') return true;
        if (*p != ':') return false;
        return digits(p + 1, 2) && *(p + 3) == '\0';
    }
    return false;
}

// --- duration -----------------------------------------------------------------

// --- numeric format -----------------------------------------------------------

// Parse a possibly-empty numeric field [begin,end) into *out; empty => not present.
static bool parse_opt_number(const char* begin, const char* end, bool* present, double* out) {
    if (begin >= end) { *present = false; return true; }   // empty field = open-ended
    float f;
    if (!parse_float_token(begin, end, &f)) return false;
    *present = true;
    *out = (double)f;
    return true;
}

bool homie_parse_number_format(const char* format, HomieNumberFormat* out) {
    HomieNumberFormat nf = {false, false, false, 0, 0, 0};
    if (!format || format[0] == '\0') { if (out) *out = nf; return true; }
    // Split on ':' into up to 3 fields: min, max, step.
    const char* min_b = format;
    const char* min_e = strchr(min_b, ':');
    if (!min_e) return false;                              // a format with no ':' is malformed
    const char* max_b = min_e + 1;
    const char* max_e = strchr(max_b, ':');
    const char* step_b = max_e ? max_e + 1 : nullptr;
    if (!max_e) max_e = max_b + strlen(max_b);
    if (!parse_opt_number(min_b, min_e, &nf.has_min, &nf.min)) return false;
    if (!parse_opt_number(max_b, max_e, &nf.has_max, &nf.max)) return false;
    if (step_b) {
        const char* step_e = step_b + strlen(step_b);
        if (!parse_opt_number(step_b, step_e, &nf.has_step, &nf.step)) return false;
        if (nf.has_step && nf.step <= 0) return false;     // step must be > 0
    }
    if (out) *out = nf;
    return true;
}

bool homie_validate_number(double value, const char* format, double* coerced) {
    HomieNumberFormat nf;
    if (!homie_parse_number_format(format, &nf)) { if (coerced) *coerced = value; return false; }
    double result = value;
    if (nf.has_step) {
        // base = min, else max, else the value itself (spec §"numeric formats and step").
        double base = nf.has_min ? nf.min : (nf.has_max ? nf.max : value);
        result = floor((value - base) / nf.step + 0.5) * nf.step + base;   // round "up" on .5
    }
    if (coerced) *coerced = result;
    if (nf.has_min && result < nf.min) return false;       // range check AFTER rounding
    if (nf.has_max && result > nf.max) return false;
    return true;
}

bool homie_validate_duration(const char* payload) {
    if (!payload || payload[0] != 'P') return false;
    const char* p = payload + 1;
    bool any = false;       // saw at least one component
    bool in_time = false;   // past the 'T' separator

    // Date part: <number>(Y|M|W|D)... then optional 'T' time part: <number>(H|M|S)...
    while (*p) {
        if (*p == 'T') {
            if (in_time) return false;                  // only one 'T'
            in_time = true;
            p++;
            if (*p == '\0') return false;               // 'T' must be followed by a time component
            continue;
        }
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) p++;         // a run of digits...
        if (*p == '.' ) {                               // fractional (allowed on the smallest unit)
            p++;
            if (!isdigit((unsigned char)*p)) return false;
            while (isdigit((unsigned char)*p)) p++;
        }
        char unit = *p;
        bool ok_unit = in_time ? (unit == 'H' || unit == 'M' || unit == 'S')
                               : (unit == 'Y' || unit == 'M' || unit == 'W' || unit == 'D');
        if (!ok_unit) return false;
        any = true;
        p++;
    }
    return any;
}
