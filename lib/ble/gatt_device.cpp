#include <Arduino.h>
#include <ble/gatt_device.h>
#include <homie/Property.h>   // entity_settable_callback(Property*) dereferences it
#include <platform/mqtt_client.h>

// A full GATT_HEX_MAX notification must survive Property's value buffer as hex.
static_assert(2 * GATT_HEX_MAX <= Property::VALUE_MAX,
              "GATT_HEX_MAX notification does not fit in a Property value as hex");

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// NVS namespaces are capped at 15 chars (NVS_NS_NAME_MAX_SIZE - 1). A node id that fits
// after "ble_" is used as is. A longer one gets "blh_", its first 3 chars, and a 32-bit
// FNV-1a hash of the whole id, so ids sharing a long prefix ("battery-pack-1" and
// "battery-pack-2") no longer truncate to the same namespace. The distinct prefix keeps
// the two forms from ever colliding with each other.
static void nvs_namespace_for(const char* node_id, char* out, size_t out_len) {
    if (strlen(node_id) <= 11) {
        snprintf(out, out_len, "ble_%s", node_id);
        return;
    }
    uint32_t h = 2166136261u;
    for (const char* p = node_id; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    snprintf(out, out_len, "blh_%.3s%08lx", node_id, (unsigned long)h);
}

bool GattDevice::setup() {
    NodeEntity::setup();

    _np_connected.setup(getNodeId(), "connected");
    _np_address.setup(getNodeId(), "address");
    _np_rssi.setup(getNodeId(), "rssi");
    _np_last_notify.setup(getNodeId(), "last-notify");
    _np_notify_count.setup(getNodeId(), "notify-count");
    _np_write.setup(getNodeId(), "write",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_rescan.setup(getNodeId(), "rescan",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);

    _link.setTag(getNodeId());
    _link.setUUIDs(_service_uuid, _notify_uuid, _write_uuid);
    _link.setNameFilter(_name_filter);
    // Per-node NVS namespace, so two GATT nodes on one board keep separate cached
    // addresses.
    char ns[16];
    nvs_namespace_for(getNodeId(), ns, sizeof(ns));
    _link.setNVSNamespace(ns);
    if (_cfg_address && *_cfg_address) _link.setAddress(_cfg_address);
    _link.onNotify(&GattDevice::on_rx, this);

    _link.begin("ESP32_GATT");

    _np_notify_count.set_value_and_publish((int)_notify_count);
    _np_connected.set_value_and_publish(_link.isConnected());
    if (_link.address()[0]) _np_address.set_value_and_publish(_link.address());
    return true;
}

void GattDevice::on_rx(void* ctx, const uint8_t* data, size_t len) {
    static_cast<GattDevice*>(ctx)->handleRx(data, len);
}

// Runs on the NimBLE task: copy and flag, nothing else. Publishing from here would put
// MQTT work on the BLE stack's task.
void GattDevice::handleRx(const uint8_t* data, size_t len) {
    if (len > GATT_HEX_MAX) len = GATT_HEX_MAX;
    memcpy(_rx, data, len);
    _rx_len = len;
    _notify_count++;
    _rx_ready = true;
}

void GattDevice::loop() {
    _link.loop();

    if (_rx_ready) {
        _rx_ready = false;
        char hex[2 * GATT_HEX_MAX + 1];   // two hex chars per byte
        size_t n = _rx_len;
        for (size_t i = 0; i < n; i++) snprintf(&hex[i * 2], 3, "%02X", _rx[i]);
        hex[n * 2] = '\0';
        Serial.printf("GATT[%s]: RX %u bytes: %s\n", getNodeId(), (unsigned)n, hex);
        _np_last_notify.set_value_and_publish(hex);
        _np_notify_count.set_value_and_publish((int)_notify_count);
    }

    // Publish link state on change, and refresh RSSI on a slow cadence.
    bool now_connected = _link.isConnected();
    if (now_connected != _last_connected) {
        _last_connected = now_connected;
        Serial.printf("GATT[%s]: %s\n", getNodeId(), now_connected ? "connected" : "disconnected");
        _np_connected.set_value_and_publish(now_connected);
        if (now_connected && _link.address()[0]) {
            _np_address.set_value_and_publish(_link.address());
        }
    }
    if (millis() - _last_status_ms > 30000) {
        _last_status_ms = millis();
        if (now_connected) _np_rssi.set_value_and_publish(_link.rssi());
        else               _np_rssi.clearValue();   // no link, no meaningful RSSI
    }
}

// Accepts "DDA50300FFFD77", optionally with spaces or colons between bytes.
bool GattDevice::sendHex(const char* hex) {
    uint8_t buf[GATT_HEX_MAX];
    size_t n = 0;
    const char* p = hex;
    while (*p && n < GATT_HEX_MAX) {
        if (*p == ' ' || *p == ':' || *p == '-') { p++; continue; }
        int hi = hex_nibble(*p++);
        if (hi < 0 || !*p) return false;
        int lo = hex_nibble(*p++);
        if (lo < 0) return false;
        buf[n++] = (uint8_t)((hi << 4) | lo);
    }
    if (n == 0) return false;
    return _link.write(buf, n);
}

void GattDevice::entity_settable_callback(Property* property) {
    if (!property) return;
    entity_settable_callback(property->id(), property->value());
}

void GattDevice::entity_settable_callback(const char* property_id, const char* value) {
    if (strcmp(property_id, "write") == 0) {
        if (!sendHex(value)) {
            Serial.printf("GATT[%s]: write '%s' failed — bad hex, no write "
                          "characteristic, or not connected\n", getNodeId(), value);
        } else {
            Serial.printf("GATT[%s]: TX %s\n", getNodeId(), value);
        }
    } else if (strcmp(property_id, "rescan") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            _link.forgetAddress();
            _np_address.clearValue();
            _np_rescan.set_value_and_publish(false);   // a trigger, not state
        }
    } else {
        Serial.printf("GATT[%s]: unhandled settable '%s'\n", getNodeId(), property_id);
    }
}
