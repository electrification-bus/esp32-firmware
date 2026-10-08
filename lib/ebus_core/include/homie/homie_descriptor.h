#pragma once
#include <homie/homie_enums.h>   // PropertyDatatype

// Declarative property descriptor (D2) — a heap-free, designated-initializer analog of
// the python-sdk's Property(from_dict=...). Declare a node's properties as a static
// const table that reads like the device.yml, then register the whole node in one call:
//
//   static Property        battery_store[5];        // mutable storage (no heap)
//   static const PropertyDesc battery[] = {
//     { .id="soc",         .name="State of Charge", .datatype=PropertyDatatype::Integer,
//       .unit=to_homie(Unit::Percent), .format="0:100" },
//     { .id="voltage",     .name="Pack Voltage",    .datatype=PropertyDatatype::Float,
//       .unit=to_homie(Unit::Volt) },
//     { .id="current",     .name="Pack Current",    .datatype=PropertyDatatype::Float,
//       .unit=to_homie(Unit::Ampere) },
//     ...
//   };
//   device->addNode("battery", "Battery", EBUS_CAP_SOC, battery, battery_store);
//
// The templated Device::addNode deduces the count and requires the descriptor table and
// the Property storage array to have the SAME size — a mismatch is a compile error.
// Omitted fields use these defaults (settable=false, retained=true, no unit/format),
// matching Homie's defaults; `type` and custom units stay free-form strings.
struct PropertyDesc {
    const char*      id;
    const char*      name;
    PropertyDatatype datatype;
    const char*      unit            = "";
    bool             settable        = false;
    bool             retained        = true;
    const char*      format          = "";
    bool             supports_target = false;   // publish $target on accepted /set (C5)
};
