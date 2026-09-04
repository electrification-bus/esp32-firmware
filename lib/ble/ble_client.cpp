#include <Arduino.h>
#include <Preferences.h>
#include "esp_log.h"
#include <ble/ble_client.h>

// NimBLE — lightweight BLE stack; supports direct connect by address, so a cached
// address skips scanning entirely on reboot.
#include <NimBLEDevice.h>

// NimBLE is a process-wide singleton: init once, whichever link gets there first.
static bool _nimble_started = false;

// Registry of live links, so the NimBLE task's callbacks (which hand us a characteristic
// or a client, not a `this`) can find the owning link. Fixed size, no heap — the same
// reason the settable-property table in mqtt_client.cpp is a fixed array.
static BLEClientLink* _links[BLECLIENT_MAX_LINKS] = {nullptr};

static void register_link(BLEClientLink* l) {
    for (int i = 0; i < BLECLIENT_MAX_LINKS; i++) {
        if (_links[i] == l) return;
        if (_links[i] == nullptr) { _links[i] = l; return; }
    }
    Serial.printf("BLE: link registry full (%d) — notifications for this device will be "
                  "DROPPED (raise BLECLIENT_MAX_LINKS)\n", BLECLIENT_MAX_LINKS);
}

// ---- NimBLE callbacks -------------------------------------------------------

static void ble_notify_trampoline(NimBLERemoteCharacteristic* pChar,
                                  uint8_t* pData, size_t length, bool isNotify) {
    (void)isNotify;
    if (pData == nullptr || length == 0) return;
    for (int i = 0; i < BLECLIENT_MAX_LINKS; i++) {
        if (_links[i] && _links[i]->_notifyCharHandle() == (void*)pChar) {
            _links[i]->_deliverNotify(pData, length);
            return;
        }
    }
}

class LinkClientCallbacks : public NimBLEClientCallbacks {
public:
    BLEClientLink* owner = nullptr;
    void onConnect(NimBLEClient* pClient) override {
        (void)pClient;
    }
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        (void)pClient;
        Serial.printf("BLE: disconnected (reason=%d)\n", reason);
        if (owner) owner->_onDisconnected();
    }
};

// ---- configuration ----------------------------------------------------------

void BLEClientLink::setUUIDs(const char* service_uuid, const char* notify_uuid,
                             const char* write_uuid) {
    snprintf(_service_uuid, sizeof(_service_uuid), "%s", service_uuid ? service_uuid : "");
    snprintf(_notify_uuid,  sizeof(_notify_uuid),  "%s", notify_uuid  ? notify_uuid  : "");
    snprintf(_write_uuid,   sizeof(_write_uuid),   "%s", write_uuid   ? write_uuid   : "");
}

void BLEClientLink::setNameFilter(const char* csv) {
    snprintf(_name_filter, sizeof(_name_filter), "%s", csv ? csv : "");
}

void BLEClientLink::setNVSNamespace(const char* ns) {
    if (ns && *ns) snprintf(_nvs_ns, sizeof(_nvs_ns), "%s", ns);
}

void BLEClientLink::setAddress(const char* address) {
    snprintf(_address, sizeof(_address), "%s", address ? address : "");
}

void BLEClientLink::setTag(const char* tag) {
    if (tag && *tag) snprintf(_tag, sizeof(_tag), "%s", tag);
}

// ---- NVS address cache ------------------------------------------------------

void BLEClientLink::loadAddress() {
    if (_address[0]) return;                 // an explicitly configured address wins
    Preferences prefs;
    prefs.begin(_nvs_ns, true);
    if (prefs.isKey("addr")) {
        char buf[BLECLIENT_ADDR_LEN] = {0};
        size_t len = prefs.getString("addr", buf, sizeof(buf));
        if (len > 0) {
            setAddress(buf);
            Serial.printf("%s: loaded cached address '%s'\n", _tag, _address);
        }
    }
    prefs.end();
}

void BLEClientLink::saveAddress(const char* address) {
    Preferences prefs;
    prefs.begin(_nvs_ns, false);
    prefs.putString("addr", address);
    prefs.end();
    Serial.printf("%s: cached address '%s'\n", _tag, address);
}

void BLEClientLink::forgetAddress() {
    Preferences prefs;
    prefs.begin(_nvs_ns, false);
    prefs.remove("addr");
    prefs.end();
    _address[0] = '\0';
    disconnect();
    _last_attempt = 0;                       // loop() rescans on the next tick
    Serial.printf("%s: cached address cleared — will rescan\n", _tag);
}

// ---- lifecycle --------------------------------------------------------------

bool BLEClientLink::begin(const char* local_device_name) {
    if (!_service_uuid[0] || !_notify_uuid[0]) {
        Serial.printf("%s: **ERROR -- setUUIDs() must be called before begin()\n", _tag);
        return false;
    }
    if (!_nimble_started) {
        Serial.printf("%s: NimBLE init (heap before: %u)\n", _tag, ESP.getFreeHeap());
        NimBLEDevice::init(local_device_name ? local_device_name : "ESP32");
        esp_log_level_set("NimBLEClient", ESP_LOG_WARN);
        _nimble_started = true;
        Serial.printf("%s: NimBLE ready (heap after: %u)\n", _tag, ESP.getFreeHeap());
    }
    register_link(this);
    _initialized = true;

    loadAddress();
    if (_address[0]) connectByAddress(_address);
    else             scanAndConnect();
    return true;
}

void BLEClientLink::loop() {
    if (!_initialized) return;
    if (isConnected()) return;

    if (millis() - _last_attempt < _reconnect_ms) return;
    Serial.printf("%s: reconnecting (last attempt %lus ago)\n",
                  _tag, (unsigned long)((millis() - _last_attempt) / 1000));
    // A known address is retried directly and never falls back to scanning: on a site
    // with several similar devices a scan could latch onto the wrong one.
    if (_address[0]) connectByAddress(_address);
    else             scanAndConnect();
}

bool BLEClientLink::isConnected() {
    NimBLEClient* c = (NimBLEClient*)_client;
    if (c == nullptr) { _connected = false; return false; }
    _connected = c->isConnected();
    return _connected;
}

void BLEClientLink::disconnect() {
    _connected = false;
    _write_char_handle = nullptr;
    _notify_char_handle = nullptr;
    NimBLEClient* c = (NimBLEClient*)_client;
    if (c != nullptr && c->isConnected()) c->disconnect();
}

void BLEClientLink::_onDisconnected() {
    _connected = false;
    _write_char_handle = nullptr;
    _notify_char_handle = nullptr;
}

void BLEClientLink::_deliverNotify(const uint8_t* data, size_t len) {
    if (_notify_cb) _notify_cb(_notify_ctx, data, len);
}

// ---- scan -------------------------------------------------------------------

// Case-insensitive "does `name` contain any comma-separated token of _name_filter".
static bool name_matches(const char* name, const char* filter) {
    if (!filter || !*filter || !name || !*name) return false;
    char lower[64] = {0};
    for (int i = 0; i < 63 && name[i]; i++) lower[i] = tolower((unsigned char)name[i]);

    const char* p = filter;
    while (*p) {
        char token[32] = {0};
        int n = 0;
        while (*p && *p != ',' && n < 31) token[n++] = tolower((unsigned char)*p++);
        while (*p && *p != ',') p++;         // skip an over-long token's tail
        if (*p == ',') p++;
        if (n > 0 && strstr(lower, token)) return true;
    }
    return false;
}

bool BLEClientLink::scanAndConnect() {
    _last_attempt = millis();
    Serial.printf("%s: scanning %ds...\n", _tag, _scan_seconds);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(_scan_seconds);

    NimBLEUUID service(_service_uuid);
    int match_idx = -1;

    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice* d = results.getDevice(i);
        const char* name = d->getName().c_str();
        Serial.printf("%s: found '%s' (%s) RSSI=%d\n", _tag, name,
                      d->getAddress().toString().c_str(), d->getRSSI());

        // Service UUID is the reliable signal; the name filter is the fallback for
        // devices that do not advertise their service.
        if (d->isAdvertisingService(service) || name_matches(name, _name_filter)) {
            match_idx = i;
            break;
        }
    }

    if (match_idx < 0) {
        // Deliberately NOT falling back to "strongest signal": connecting to an arbitrary
        // nearby device and discovering it lacks the service wastes a connection window,
        // and on a busy site it can repeatedly latch onto a neighbour's hardware.
        Serial.printf("%s: no device matched service %s%s%s\n", _tag, _service_uuid,
                      _name_filter[0] ? " or name filter " : "", _name_filter);
        return false;
    }

    char addr[BLECLIENT_ADDR_LEN];
    snprintf(addr, sizeof(addr), "%s",
             results.getDevice(match_idx)->getAddress().toString().c_str());
    _rssi = results.getDevice(match_idx)->getRSSI();

    if (connectByAddress(addr)) {
        saveAddress(addr);
        return true;
    }
    return false;
}

// ---- connect ----------------------------------------------------------------

bool BLEClientLink::connectByAddress(const char* address) {
    _last_attempt = millis();
    setAddress(address);
    Serial.printf("%s: connecting to %s\n", _tag, _address);

    NimBLEClient* c = (NimBLEClient*)_client;
    if (c == nullptr) {
        c = NimBLEDevice::createClient();
        LinkClientCallbacks* cb = new LinkClientCallbacks();   // one per link, never freed
        cb->owner = this;
        _client_callbacks = cb;
        c->setClientCallbacks(cb, false);
        c->setConnectionParams(12, 12, 0, 200);
        c->setConnectTimeout(5);
        _client = c;
    }

    uint32_t t0 = millis();
    if (!c->connect(NimBLEAddress(std::string(address), BLE_ADDR_PUBLIC))) {
        Serial.printf("%s: connect failed (%lums)\n", _tag, (unsigned long)(millis() - t0));
        return false;
    }

    NimBLERemoteService* service = c->getService(NimBLEUUID(_service_uuid));
    if (service == nullptr) {
        Serial.printf("%s: service %s not found — wrong device?\n", _tag, _service_uuid);
        c->disconnect();
        return false;
    }

    if (_write_uuid[0]) {
        NimBLERemoteCharacteristic* w = service->getCharacteristic(NimBLEUUID(_write_uuid));
        if (w == nullptr || (!w->canWrite() && !w->canWriteNoResponse())) {
            Serial.printf("%s: write characteristic %s missing or not writable\n",
                          _tag, _write_uuid);
            // List what IS there — the single most useful line when bringing up an
            // unfamiliar device with a wrong UUID in its config.
            for (size_t i = 0; i < service->getCharacteristics().size(); i++) {
                Serial.printf("%s:   available char: %s\n", _tag,
                              service->getCharacteristics()[i]->getUUID().toString().c_str());
            }
            c->disconnect();
            return false;
        }
        _write_char_handle = (void*)w;
    }

    NimBLERemoteCharacteristic* n = service->getCharacteristic(NimBLEUUID(_notify_uuid));
    if (n == nullptr || !n->canNotify()) {
        Serial.printf("%s: notify characteristic %s missing or not subscribable\n",
                      _tag, _notify_uuid);
        c->disconnect();
        return false;
    }
    // Registered BEFORE subscribing: the trampoline resolves the owning link by matching
    // this handle, and a notification can arrive the moment the subscription lands.
    _notify_char_handle = (void*)n;
    if (!n->subscribe(true, ble_notify_trampoline)) {
        Serial.printf("%s: failed to subscribe to %s\n", _tag, _notify_uuid);
        _notify_char_handle = nullptr;
        c->disconnect();
        return false;
    }

    _connected = true;
    _last_success = millis();
    Serial.printf("%s: connected and ready (%s, %lums)\n", _tag, _address,
                  (unsigned long)(millis() - t0));
    return true;
}

// ---- write ------------------------------------------------------------------

bool BLEClientLink::write(const uint8_t* data, size_t len) {
    if (!isConnected() || _write_char_handle == nullptr) return false;
    return ((NimBLERemoteCharacteristic*)_write_char_handle)->writeValue(data, len);
}
