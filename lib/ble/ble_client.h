#pragma once
#include <Arduino.h>

// Generic BLE GATT client link — the BLE equivalent of lib/platform/modbus.
//
// Everything here is device-agnostic: NimBLE lifecycle, scanning, connect-by-address,
// service/characteristic discovery, notification plumbing, reconnect backoff, and caching
// the resolved address in NVS so a reboot reconnects without scanning.
//
// A driver supplies three UUIDs and gets a byte pipe. It never touches NimBLE.
//
//     _link.setUUIDs(SERVICE, NOTIFY, WRITE);
//     _link.onNotify(&MyDriver::on_rx, this);   // fn-ptr + ctx, no std::function
//     _link.begin("ESP32_MYDEV");
//     ...
//     _link.write(cmd, sizeof(cmd));
//
// House style: static allocation, no String, no STL containers, callbacks are plain
// function pointers plus an opaque context (see include/node/NodeProperty.h `on_set`).
//
// Worked examples: lib/ble/gatt_device.* (generic) and examples/bms/ (JBD BMS protocol
// built on this link).

// BLECLIENT_ prefix, not BLE_: the NimBLE host defines BLE_MAX_LINKS (and a good deal
// else under BLE_*) via its syscfg headers, and an unprefixed name here silently
// redefines it for every translation unit that includes both.
#define BLECLIENT_ADDR_LEN   18     // "XX:XX:XX:XX:XX:XX" + NUL
#define BLECLIENT_UUID_LEN   40
#define BLECLIENT_MAX_LINKS       4     // concurrent BLE connections this build supports
#define BLECLIENT_NAME_FILTER_LEN 64
#define BLECLIENT_ADDR_TYPE_UNKNOWN 0xFF  // not yet learned; connect alternates public/random

// Notification payload callback: fn-ptr + opaque context, fired from the NimBLE task.
// Keep it short — copy the bytes and return; do not block or publish MQTT from here.
typedef void (*ble_notify_cb_t)(void* ctx, const uint8_t* data, size_t len);

class BLEClientLink {
public:
    BLEClientLink() {}
    ~BLEClientLink() {}

    // --- configuration (call before begin()) ---

    // GATT service plus the characteristics to subscribe to and write to. write_uuid may
    // be empty for a notify-only device.
    void setUUIDs(const char* service_uuid, const char* notify_uuid, const char* write_uuid);

    // Comma-separated, case-insensitive substrings matched against advertised names when
    // scanning, e.g. "xiaoxiang,jbd,bms". Empty means match on service UUID only.
    void setNameFilter(const char* csv);

    // NVS namespace for the cached address. Give each link its own so two BLE devices on
    // one board do not overwrite each other. Default "ble_link".
    void setNVSNamespace(const char* ns);

    // Pre-seed the address (e.g. from device.yml config), skipping the scan entirely.
    // A bare address does not say whether it is public or random, so until one connects
    // the attempts alternate between the two.
    void setAddress(const char* address);

    // Log prefix, so a board with two links produces readable output. Default "BLE".
    void setTag(const char* tag);

    void setScanSeconds(uint8_t s)          { _scan_seconds = s; }
    void setReconnectInterval(uint32_t ms)  { _reconnect_ms = ms; }

    // --- lifecycle ---

    // Initialise NimBLE (once per firmware, whichever link calls first), load any cached
    // address from NVS, and attempt the first connection. Returns false if NimBLE could
    // not start; a failed *connection* is not an error — loop() retries.
    bool begin(const char* local_device_name);

    // Drive reconnect backoff. Call every loop() tick; it returns immediately when
    // connected.
    void loop();

    bool isConnected();
    void disconnect();

    // Drop the cached address and force a fresh scan on the next attempt.
    void forgetAddress();

    // --- data ---

    void onNotify(ble_notify_cb_t cb, void* ctx) { _notify_cb = cb; _notify_ctx = ctx; }
    bool write(const uint8_t* data, size_t len);

    const char* address() const { return _address; }
    int  rssi() const           { return _rssi; }
    bool everConnected() const  { return _last_success != 0; }

    // Called from the NimBLE task via the static trampoline — public so the file-scope
    // callback can reach it, not part of the driver-facing API.
    void _deliverNotify(const uint8_t* data, size_t len);
    void _onDisconnected();
    void* _notifyCharHandle() const { return _notify_char_handle; }

private:
    bool connectByAddress();
    bool scanAndConnect();
    void loadAddress();
    void saveAddress();
    void saveAddressType();

    char _service_uuid[BLECLIENT_UUID_LEN] = {0};
    char _notify_uuid[BLECLIENT_UUID_LEN]  = {0};
    char _write_uuid[BLECLIENT_UUID_LEN]   = {0};
    char _name_filter[BLECLIENT_NAME_FILTER_LEN] = {0};
    char _nvs_ns[16]   = "ble_link";
    char _tag[16]      = "BLE";
    char _address[BLECLIENT_ADDR_LEN] = {0};
    uint8_t _addr_type = BLECLIENT_ADDR_TYPE_UNKNOWN;   // BLE_ADDR_* once known
    bool    _try_random = false;   // next guess while _addr_type is unknown

    uint8_t  _scan_seconds  = 5;
    uint32_t _reconnect_ms  = 30000;

    // Opaque NimBLE handles — kept as void* so this header pulls in no NimBLE types and
    // a driver including it does not inherit the whole stack.
    void* _client              = nullptr;
    void* _write_char_handle   = nullptr;
    void* _notify_char_handle  = nullptr;

    ble_notify_cb_t _notify_cb  = nullptr;
    void*           _notify_ctx = nullptr;

    int      _slot        = -1;    // index in the file-static link registry, and
                                   // into the matching NimBLE callbacks pool
    bool     _initialized = false;
    bool     _connected   = false;
    int      _rssi        = 0;
    uint32_t _last_success = 0;
    uint32_t _last_attempt = 0;
};
