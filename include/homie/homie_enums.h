#pragma once
#include <homie/homie.h>   // HOMIE_DATATYPE_* string macros (single source of truth)

// Type-safe authoring enums (D1). Stringly-typed datatype/unit arguments are easy to
// mistype ("flaot", "%C") and the compiler can't catch it; these scoped enums make the
// device-authoring API checkable at compile time. Each has a to_homie() that yields the
// exact on-the-wire string. `type` and custom units stay free-form — these
// are convenience, not a constraint (you can always pass a raw string).

// --- Property datatype (the 9 Homie 5 datatypes) -----------------------------
enum class PropertyDatatype { Boolean, Integer, Float, String, Enum, Color, Datetime, Duration, Json };

inline const char* to_homie(PropertyDatatype d) {
    switch (d) {
        case PropertyDatatype::Boolean:  return HOMIE_DATATYPE_BOOLEAN;
        case PropertyDatatype::Integer:  return HOMIE_DATATYPE_INTEGER;
        case PropertyDatatype::Float:    return HOMIE_DATATYPE_FLOAT;
        case PropertyDatatype::String:   return HOMIE_DATATYPE_STRING;
        case PropertyDatatype::Enum:     return HOMIE_DATATYPE_ENUM;
        case PropertyDatatype::Color:    return HOMIE_DATATYPE_COLOR;
        case PropertyDatatype::Datetime: return HOMIE_DATATYPE_DATETIME;
        case PropertyDatatype::Duration: return HOMIE_DATATYPE_DURATION;
        case PropertyDatatype::Json:     return HOMIE_DATATYPE_JSON;
    }
    return "";
}

// --- Units (transcribed from the python-sdk Unit table) ----------------------
// Homie's recommended unit strings plus the eBus additions (var, Wh). UTF-8 literals;
// `unit` is free-form on the wire, so custom units just pass a string instead.
enum class Unit {
    DegreeCelsius, DegreeFahrenheit, Degree, Liter, Gallon, Volt, Watt, Kilowatt,
    KilowattHour, Ampere, Hertz, Rpm, Percent, Meter, CubicMeter, Feet,
    MetersPerSecond, Knots, Pascal, Psi, Ppm, Second, Minute, Hour, Lux, Kelvin,
    Mired, Count, Var, WattHour
};

inline const char* to_homie(Unit u) {
    switch (u) {
        case Unit::DegreeCelsius:    return "°C";       // °C
        case Unit::DegreeFahrenheit: return "°F";       // °F
        case Unit::Degree:           return "°";        // °
        case Unit::Liter:            return "L";
        case Unit::Gallon:           return "gal";
        case Unit::Volt:             return "V";
        case Unit::Watt:             return "W";
        case Unit::Kilowatt:         return "kW";
        case Unit::KilowattHour:     return "kWh";
        case Unit::Ampere:           return "A";
        case Unit::Hertz:            return "Hz";
        case Unit::Rpm:              return "rpm";
        case Unit::Percent:          return "%";
        case Unit::Meter:            return "m";
        case Unit::CubicMeter:       return "m³";       // m³
        case Unit::Feet:             return "ft";
        case Unit::MetersPerSecond:  return "m/s";
        case Unit::Knots:            return "kn";
        case Unit::Pascal:           return "Pa";
        case Unit::Psi:              return "psi";
        case Unit::Ppm:              return "ppm";
        case Unit::Second:           return "s";
        case Unit::Minute:           return "min";
        case Unit::Hour:             return "h";
        case Unit::Lux:              return "lx";
        case Unit::Kelvin:           return "K";
        case Unit::Mired:            return "MK⁻¹"; // MK⁻¹
        case Unit::Count:            return "#";
        case Unit::Var:              return "var";           // volt-ampere reactive (eBus addition)
        case Unit::WattHour:         return "Wh";
    }
    return "";
}
