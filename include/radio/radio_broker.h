#pragma once
#include <Arduino.h>

// Radio types that can be requested by nodes
enum RadioType {
    RADIO_WIFI = 0,
    RADIO_BT,
    RADIO_TYPE_COUNT
};

// Grant result
enum RadioGrant {
    RADIO_GRANT_DENIED = 0,
    RADIO_GRANT_ALLOWED
};

#define RADIO_MAX_REQUESTS 8

// Register a radio need. Call in node constructor (static init safe).
// requester_id: human-readable tag for logging (e.g., "battery_bms")
void radio_request(RadioType type, const char* requester_id);

// Resolve all requests against platform capabilities.
// Call once in main setup() after ethernet status is known,
// before WiFi STA init and before node setup().
void radio_resolve();

// Query whether a specific radio type was granted.
// Nodes call this in setup() to decide whether to proceed.
RadioGrant radio_query(RadioType type);

// Perform WiFi teardown if BT was granted on shared-radio boards.
// Call in main setup() after radio_resolve(), before node setup().
void radio_apply();

// Print resolution summary to Serial
void radio_print_status();
