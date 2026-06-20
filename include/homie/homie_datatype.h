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
// in `format`. No spaces permitted. Component count + inclusive ranges per type:
//   rgb -> 3 floats 0..255 ; hsv -> 0..360, 0..100, 0..100 ; xyz -> 2 floats 0..1.
bool homie_validate_color(const char* payload, const char* format);

// datetime: ISO 8601 timestamp. Pragmatic check — requires a calendar date
// YYYY-MM-DD, optionally followed by 'T' and a time (hh:mm[:ss][.fff]) and a zone
// (Z or ±hh[:mm]). Field ranges are checked; full leap-year/day-of-month is not.
bool homie_validate_datetime(const char* payload);

// duration: ISO 8601 duration. Accepts PnYnMnWnD and PT-time PnHnMnS forms, e.g.
// "PT12H5M46S", "PT5M", "P3D". Requires 'P' first and at least one component.
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
