#pragma once
// The process-wide Device instances, and nothing else.
//
// This exists so a header can reach theDevice without pulling in homie/Device.h, which
// carries ArduinoJson (~20,000 preprocessed lines) for its serialize()/from_dict()
// members. node/NodeEntity.h is the reason: it reaches every driver, and it only ever
// needs Device as a POINTER plus the address of theDevice — neither of which requires the
// complete type.
//
// A translation unit that actually calls Device methods includes homie/Device.h itself.
// Both symbols are defined in src/platform/config.cpp.

class Device;

extern Device theDevice;

// The device whose entity is currently running setup() — set by NodeEntity::setup()
// (which every driver chains to first) so NodeProperty::setup() builds topics and
// resolves properties against the entity's OWN device (root or a nested child) instead
// of the global theDevice. Null falls back to theDevice (single-device default).
extern Device* g_active_setup_device;
