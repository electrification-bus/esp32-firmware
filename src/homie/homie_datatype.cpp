#include <homie/homie_datatype.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

// --- helpers ------------------------------------------------------------------

// Strict lexer for the Homie float format over [begin,end): an optional '-', digits
// with at most one '.', at least one digit, then an optional exponent 'e'/'E' with an
// optional '-' and at least one digit. A leading '+' is not part of the format
// (Payloads/Float). On success writes the 64-bit value and returns true. Rejects
// spaces and stray characters: the caller has already split on ',' or ':' so a token
// must be a bare number. Values that overflow a double are rejected.
static bool parse_float_token(const char* begin, const char* end, double* out) {
    if (begin >= end) return false;
    const char* p = begin;
    bool any_digit = false, dot = false;
    if (*p == '-') p++;
    for (; p < end && *p != 'e' && *p != 'E'; ++p) {
        if (*p >= '0' && *p <= '9') { any_digit = true; }
        else if (*p == '.' && !dot) { dot = true; }
        else return false;
    }
    if (!any_digit) return false;
    if (p < end) {                                      // exponent
        p++;
        if (p < end && *p == '-') p++;
        if (p >= end) return false;
        for (; p < end; ++p) if (*p < '0' || *p > '9') return false;
    }
    char buf[64];
    size_t n = (size_t)(end - begin);
    if (n >= sizeof(buf)) return false;
    memcpy(buf, begin, n);
    buf[n] = '\0';
    double v = strtod(buf, nullptr);
    if (!isfinite(v)) return false;
    *out = v;
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
    double vals[3];
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
        out->c[0] = (float)vals[0];
        out->c[1] = (float)vals[1];
        out->c[2] = (want == 3) ? (float)vals[2] : 0.0f;
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

// Days in `month` (1..12) of `year`, Gregorian leap-year rule.
static int days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return days[month - 1];
}

bool homie_validate_datetime(const char* payload) {
    if (!payload) return false;
    const char* p = payload;
    // Calendar date: YYYY-MM-DD (required).
    if (!digits(p, 4) || p[4] != '-' || !digits(p + 5, 2) || p[7] != '-' || !digits(p + 8, 2))
        return false;
    int year  = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
    int month = (p[5] - '0') * 10 + (p[6] - '0');
    int day   = (p[8] - '0') * 10 + (p[9] - '0');
    if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month)) return false;
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
    if (!parse_float_token(begin, end, out)) return false;
    *present = true;
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

// --- numeric payloads -----------------------------------------------------------

bool homie_parse_float_payload(const char* payload, double* out) {
    if (!payload) return false;
    // The same lexer the $format fields use, over the whole string. It already rejects
    // '+', embedded spaces, hex, inf/nan and anything non-finite, so nothing here has to
    // re-state the rules — and the format side and the payload side cannot drift apart.
    double v = 0;
    if (!parse_float_token(payload, payload + strlen(payload), &v)) return false;
    if (out) *out = v;
    return true;
}

bool homie_parse_integer_payload(const char* payload, int32_t* out) {
    if (!payload) return false;
    const char* p = payload;
    if (*p == '-') p++;
    if (*p == '\0') return false;                   // "-" alone, or an empty payload
    for (const char* q = p; *q; ++q) {
        if (*q < '0' || *q > '9') return false;      // no '.', no exponent, no '+', no space
    }
    // Digits only at this point, so the sign is the only thing strtoll can read besides
    // them; it is used for the value and for the overflow report. errno is not consulted
    // because the explicit int32_t bounds below are stricter than ERANGE on a 64-bit long.
    long long v = strtoll(payload, nullptr, 10);
    if (v < INT32_MIN || v > INT32_MAX) return false;   // refuse rather than wrap
    if (out) *out = (int32_t)v;
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
    // Homie 5 Payloads/Duration: PTxHxMxS. 'P' and 'T' are required; each of H, M, S
    // is optional but appears at most once and in that order.
    if (!payload || payload[0] != 'P' || payload[1] != 'T') return false;
    const char* p = payload + 2;
    const char* units = "HMS";
    int next = 0;           // index in `units` of the earliest unit still allowed
    bool any = false;       // saw at least one component
    while (*p) {
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) p++;         // a run of digits...
        if (*p == '.') {                                // ...with an optional fraction
            p++;
            if (!isdigit((unsigned char)*p)) return false;
            while (isdigit((unsigned char)*p)) p++;
        }
        int u = next;
        while (u < 3 && units[u] != *p) u++;
        if (u == 3) return false;                       // unknown, repeated, or out of order
        next = u + 1;
        any = true;
        p++;
    }
    return any;
}
