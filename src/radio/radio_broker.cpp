#include <radio/radio_broker.h>
#include <network/network.h>

// Request table
static struct {
    RadioType type;
    const char* requester_id;
} _requests[RADIO_MAX_REQUESTS];
static int _num_requests = 0;

// Grant table (indexed by RadioType)
static RadioGrant _grants[RADIO_TYPE_COUNT] = { RADIO_GRANT_DENIED, RADIO_GRANT_DENIED };
static bool _resolved = false;

void radio_request(RadioType type, const char* requester_id) {
    if (_num_requests >= RADIO_MAX_REQUESTS) {
        Serial.printf("RADIO: Request table full, ignoring %s\n", requester_id);
        return;
    }
    _requests[_num_requests].type = type;
    _requests[_num_requests].requester_id = requester_id;
    _num_requests++;
}

void radio_resolve() {
    bool bt_requested = false;
    bool wifi_requested = false;

    for (int i = 0; i < _num_requests; i++) {
        if (_requests[i].type == RADIO_BT) bt_requested = true;
        if (_requests[i].type == RADIO_WIFI) wifi_requested = true;
    }

    // Default: deny all
    _grants[RADIO_WIFI] = RADIO_GRANT_DENIED;
    _grants[RADIO_BT] = RADIO_GRANT_DENIED;

#ifdef CONFIG_ETH_ENABLED
    // Board has ethernet hardware
    bool has_eth = client_connected();  // ethernet (or WiFi) got an IP

    if (has_eth) {
        // Ethernet provides MQTT transport
        if (bt_requested) {
            // BT wins — WiFi radio surrendered on classic ESP32 (shared radio)
            _grants[RADIO_BT] = RADIO_GRANT_ALLOWED;
            _grants[RADIO_WIFI] = RADIO_GRANT_DENIED;
        } else {
            // No BT contention — WiFi available if requested
            _grants[RADIO_WIFI] = wifi_requested ? RADIO_GRANT_ALLOWED : RADIO_GRANT_DENIED;
        }
    } else {
        // ETH hardware present but no link — need WiFi for connectivity
        _grants[RADIO_WIFI] = RADIO_GRANT_ALLOWED;
        _grants[RADIO_BT] = RADIO_GRANT_DENIED;
        if (bt_requested) {
            Serial.println("RADIO: BT denied — ethernet down, WiFi required for MQTT");
        }
    }
#else
    // No ethernet — WiFi is the only network transport
    _grants[RADIO_WIFI] = RADIO_GRANT_ALLOWED;
    _grants[RADIO_BT] = RADIO_GRANT_DENIED;
    if (bt_requested) {
        Serial.println("RADIO: BT denied — no ethernet, WiFi required for MQTT");
    }
#endif

    _resolved = true;
}

RadioGrant radio_query(RadioType type) {
    if (!_resolved) {
        Serial.println("RADIO: WARNING — query before resolve()");
        return RADIO_GRANT_DENIED;
    }
    if (type >= RADIO_TYPE_COUNT) return RADIO_GRANT_DENIED;
    return _grants[type];
}

void radio_apply() {
    if (!_resolved) return;

#ifdef CONFIG_ETH_ENABLED
    if (_grants[RADIO_BT] == RADIO_GRANT_ALLOWED &&
        _grants[RADIO_WIFI] == RADIO_GRANT_DENIED) {
        // NimBLE coexists with WiFi on ESP32 — no need to disable WiFi.
        // WiFi STA is never started on POE-ISO (ethernet provides connectivity),
        // so the radio is already available for BLE without teardown.
        // WiFi.disconnect()/WiFi.mode(WIFI_OFF) breaks WiFiClient sockets
        // used by MQTT over ethernet — do not call them.
        Serial.println("RADIO: BT granted (WiFi STA not started, radio available for BLE)");
    }
#endif

    radio_print_status();
}

void radio_print_status() {
    Serial.println("=== RADIO BROKER ===");

    for (int i = 0; i < _num_requests; i++) {
        const char* type_str = (_requests[i].type == RADIO_BT) ? "BT" : "WiFi";
        Serial.printf("  request: %s needs %s\n", _requests[i].requester_id, type_str);
    }

    Serial.printf("  WiFi: %s\n",
        _grants[RADIO_WIFI] == RADIO_GRANT_ALLOWED ? "GRANTED" : "DENIED");
    Serial.printf("  BT:   %s\n",
        _grants[RADIO_BT] == RADIO_GRANT_ALLOWED ? "GRANTED" : "DENIED");

    Serial.println("====================");
}
