#pragma once
#include <stddef.h>

// Homie 5 payload validation for the datatypes that need structural checks —
// enum, color, datetime, duration (convention §"Payloads"). Pure functions; the
// device uses them to validate settable payloads and reject invalid ones (C1).
// boolean/integer/float/string are validated inline in Property (range/format
// enforcement for numeric types is C2).

// enum: payload must EXACTLY equal one of the comma-separated values in `format`
// (case-sensitive, leading/trailing whitespace significant). An empty payload or an
// empty/absent format is invalid (enum requires a format per spec).
bool homie_validate_enum(const char* payload, const char* format);

// color: payload is "<type>,<float>,...". `type` ∈ {rgb,hsv,xyz} AND must be listed
// in `format`. No spaces permitted. Components use the Homie float format (optional
// '-', digits, one optional '.', optional e/E exponent; no '+'). Component count +
// inclusive ranges per type:
//   rgb -> 3 floats 0..255 ; hsv -> 0..360, 0..100, 0..100 ; xyz -> 2 floats 0..1.
bool homie_validate_color(const char* payload, const char* format);

// datetime: ISO 8601 timestamp. Pragmatic check — requires a calendar date
// YYYY-MM-DD, optionally followed by 'T' and a time (hh:mm[:ss][.fff]) and a zone
// (Z or ±hh[:mm]). Field ranges are checked, including day-of-month and leap years.
bool homie_validate_datetime(const char* payload);

// duration: the Homie 5 PTxHxMxS form of an ISO 8601 duration, e.g. "PT12H5M46S",
// "PT5M". 'P' and 'T' are required; H, M and S are each optional, at most once, in
// that order, with at least one present. Date components ("P3D") are rejected.
bool homie_validate_duration(const char* payload);

// Parsed color value. `type` is 0=rgb, 1=hsv, 2=xyz; the components hold r,g,b /
// h,s,v / x,y respectively (z for xyz is derived as 1-x-y). Returns false if the
// payload is not a valid color (same rules as homie_validate_color, sans the format
// membership check — pass an empty/null format to skip it).
struct HomieColor {
    int   type;        // 0=rgb, 1=hsv, 2=xyz
    float c[3];        // rgb: r,g,b | hsv: h,s,v | xyz: x,y,(z derived)
};
bool homie_parse_color(const char* payload, const char* format, HomieColor* out);

// --- numeric format: float/integer "[min]:[max][:step]" (C2) ---

// Parsed numeric format. Missing min/max are open-ended (has_* = false). A step of 0
// means "no step". An empty/absent/":" format yields all-false (unconstrained).
struct HomieNumberFormat {
    bool   has_min, has_max, has_step;
    double min, max, step;
};

// Parse "[min]:[max][:step]" into `out`. Each field uses the Homie float format and
// is parsed as a double. Returns false only on a malformed format (e.g. step <= 0,
// or non-numeric fields); a fully-open ":" parses true with no
// constraints. A null/empty format also parses true (unconstrained).
bool homie_parse_number_format(const char* format, HomieNumberFormat* out);

// Validate `value` against `format` per the spec: round to the nearest step (if any)
// using floor((v-base)/step + 0.5)*step + base with base = min, else max, else value;
// then require min <= result <= max (inclusive). Writes the coerced (step-rounded)
// value to *coerced when non-null. Returns true iff the result is in range. With no
// format constraints, returns true and coerced = value.
bool homie_validate_number(double value, const char* format, double* coerced);
