#include <Arduino.h>
#include <time.h>
#include "esp_log.h"
#include <config.h>
#include <ble/serial_bms.h>
#include <platform/mqtt_client.h>
#include <Preferences.h>

// NimBLE — lightweight BLE stack, supports direct connect by address
#include <NimBLEDevice.h>

#define MOCK_DEBUG_BMS false

// JBD BMS BLE GATT UUIDs
static NimBLEUUID BMS_SERVICE_UUID("0000ff00-0000-1000-8000-00805f9b34fb");
static NimBLEUUID BMS_NOTIFY_UUID("0000ff01-0000-1000-8000-00805f9b34fb");
static NimBLEUUID BMS_WRITE_UUID("0000ff02-0000-1000-8000-00805f9b34fb");

// JBD BMS protocol commands (from BmsProtocol.kt)
// Frame: 0xDD, R/W, reg, len, data..., checksum_hi, checksum_lo, 0x77
static const uint8_t CMD_BASIC_INFO[]    = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
static const uint8_t CMD_CELL_VOLTAGES[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};
// static const uint8_t CMD_HARDWARE_VER[]  = {0xDD, 0xA5, 0x05, 0x00, 0xFF, 0xFB, 0x77};

static const uint8_t CMD_MOSFET_ALL_ON[]  = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x00, 0xFF, 0x1D, 0x77};
static const uint8_t CMD_MOSFET_CHG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x01, 0xFF, 0x1C, 0x77};
static const uint8_t CMD_MOSFET_DSG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x02, 0xFF, 0x1B, 0x77};
static const uint8_t CMD_MOSFET_ALL_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x03, 0xFF, 0x1A, 0x77};

// NVS preferences
#define BMS_PREFS_NAMESPACE "bms_ble"
#define BMS_PREFS_KEY_ADDR  "addr"

// Singleton pointer for BLE notification callback context
static SerialBMS* _active_bms = nullptr;

// NimBLE client (persists across reconnects)
static NimBLEClient* _ble_client = nullptr;
static NimBLERemoteCharacteristic* _write_char = nullptr;
static NimBLERemoteCharacteristic* _notify_char = nullptr;

// ---- NimBLE notification callback ----

static void ble_notify_callback(
    NimBLERemoteCharacteristic* pChar,
    uint8_t* pData,
    size_t length,
    bool isNotify
) {
    if (_active_bms == nullptr || pData == nullptr || length == 0) return;
    _active_bms->onNotifyData(pData, length);
}

// ---- NimBLE client callbacks ----

class BmsClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("BMS BLE: GATT connected");
    }
    void onDisconnect(NimBLEClient* pClient) override {
        Serial.printf("BMS BLE: Disconnected (reason=%d)\n", pClient->getLastError());
        if (_active_bms) {
            _active_bms->disconnect();
        }
    }
};

static BmsClientCallbacks _client_callbacks;

// ---- Notification data handler ----

void SerialBMS::onNotifyData(const uint8_t* data, size_t length) {
    //Serial.printf("BMS BLE: RX %d bytes\n", length);
    for (size_t i = 0; i < length; i++) {
        if (_rx_len < BMS_FRAME_MAX) {
            _rx_buf[_rx_len++] = data[i];
        }
    }

    // Check for complete frame: starts with 0xDD, ends with 0x77
    if (_rx_len >= 7 &&
        _rx_buf[0] == 0xDD &&
        _rx_buf[_rx_len - 1] == 0x77) {
        Serial.printf("BMS BLE: Frame complete, %d bytes total\n", _rx_len);
        _frame_ready = true;
    }
}

// ---- Constructor ----

SerialBMS::SerialBMS(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _ssr = nullptr;
    _peak_state_known    = false;
    _last_peak_state     = false;
    _in_discharge_phase  = false;
    _connected = false;
    _ble_address[0] = '\0';
    _rx_len = 0;
    _frame_ready = false;
    memset(&_basic_info, 0, sizeof(_basic_info));
    memset(&_cell_voltages, 0, sizeof(_cell_voltages));
    _poll_timestamp = 0;
    _last_success = 0;
    _last_failure = 0;
    _poll_interval = BMS_POLL_INTERVAL;
    _last_reconnect_attempt = 0;
    _ble_initialized = false;

}

void SerialBMS::setBleAddress(const char* address) {
    strncpy(_ble_address, address, sizeof(_ble_address) - 1);
    _ble_address[sizeof(_ble_address) - 1] = '\0';
}

// ---- NVS Preferences ----

void SerialBMS::loadPreferredAddress() {
    Preferences prefs;
    prefs.begin(BMS_PREFS_NAMESPACE, true);  // read-only
    if (!prefs.isKey(BMS_PREFS_KEY_ADDR)) {
        prefs.end();
        _ble_address[0] = '\0';
        Serial.println("BMS BLE: No preferred address in NVS");
        return;
    }
    char buf[18] = {0};
    size_t len = prefs.getString(BMS_PREFS_KEY_ADDR, buf, sizeof(buf));
    prefs.end();

    if (len > 0) {
        setBleAddress(buf);
        Serial.printf("BMS BLE: Loaded preferred address '%s' from NVS\n", _ble_address);
    }
}

void SerialBMS::savePreferredAddress(const char* address) {
    Preferences prefs;
    prefs.begin(BMS_PREFS_NAMESPACE, false);
    prefs.putString(BMS_PREFS_KEY_ADDR, address);
    prefs.end();
    Serial.printf("BMS BLE: Saved preferred address '%s' to NVS\n", address);
}

void SerialBMS::clearPreferredAddress() {
    Preferences prefs;
    prefs.begin(BMS_PREFS_NAMESPACE, false);
    prefs.remove(BMS_PREFS_KEY_ADDR);
    prefs.end();
    _ble_address[0] = '\0';
    Serial.println("BMS BLE: Cleared preferred address from NVS");
}

// ---- Setup ----

bool SerialBMS::setup() {
    Serial.println("=== BMS BLE SETUP START ===");
    NodeEntity::setup();

    // Setup Homie node properties
    _np_voltage.setup(getNodeId(), "voltage");
    _np_current.setup(getNodeId(), "current");
    _np_soc.setup(getNodeId(), "soc");
    _np_remain_cap.setup(getNodeId(), "remain_capacity");
    _np_nominal_cap.setup(getNodeId(), "nominal_capacity");
    _np_cycles.setup(getNodeId(), "cycles");
    _np_mosfet_charge.setup(getNodeId(), "mosfet_charge",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_mosfet_discharge.setup(getNodeId(), "mosfet_discharge");
    _np_temperature.setup(getNodeId(), "temperature");
    _np_cell_voltages.setup(getNodeId(), "cell_voltages");
    
    //settables
    _np_charge_control.setup(getNodeId(), "charge_control",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_rescan.setup(getNodeId(), "rescan",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_charge_threshold_low.setup(getNodeId(), "charge_threshold_low",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this); 
    _np_charge_threshold_high.setup(getNodeId(), "charge_threshold_high",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_peak_begin_min.setup(getNodeId(), "peak_begin_min",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_peak_end_min.setup(getNodeId(), "peak_end_min",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_threshold_discharge_daily.setup(getNodeId(), "threshold_discharge_daily",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);

    // Publish initial values of settable properties so broker has retained state
    _np_charge_threshold_low.publish();
    _np_charge_threshold_high.publish();
    _np_peak_begin_min.publish();
    _np_peak_end_min.publish();
    _np_threshold_discharge_daily.publish();

    Serial.printf("BMS BLE: Free heap before init: %u bytes\n", ESP.getFreeHeap());

    // Initialize NimBLE
    NimBLEDevice::init("ESP32_BMS");
    esp_log_level_set("NimBLEClient", ESP_LOG_WARN);
    _ble_initialized = true;
    Serial.printf("BMS BLE: NimBLE initialized (heap after: %u)\n", ESP.getFreeHeap());

    _poll_timestamp = millis();

    // Load preferred BMS address from NVS
    loadPreferredAddress();

    // Attempt initial connection
    scanAndConnect();

    Serial.println("=== BMS BLE SETUP COMPLETE ===");
    return true;
}

// ---- BLE Scan and Connect ----

bool SerialBMS::scanAndConnect() {
    if (!_ble_initialized) return false;

    _last_reconnect_attempt = millis();

    if (_ble_address[0] != '\0') {
        // Preferred address known — retry it, never fall back to scan
        Serial.printf("BMS BLE: Connecting to bms %s\n", _ble_address);
        return connectToDevice(_ble_address);
    }

    // No preferred address — scan for a BMS device
    Serial.printf("BMS BLE: Scanning for BLE devices (%ds)...\n", BMS_SCAN_DURATION);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->start(BMS_SCAN_DURATION);

    int best_rssi = -999;
    int best_idx = -1;
    int match_idx = -1;

    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        const char* name = device.getName().c_str();
        char addr_str[18];
        strncpy(addr_str, device.getAddress().toString().c_str(), sizeof(addr_str) - 1);
        addr_str[sizeof(addr_str) - 1] = '\0';
        int rssi = device.getRSSI();

        Serial.printf("BMS BLE: Scan found: '%s' (%s) RSSI=%d\n", name, addr_str, rssi);

        if (device.isAdvertisingService(BMS_SERVICE_UUID)) {
            Serial.printf("BMS BLE: Service UUID match: %s\n", addr_str);
            match_idx = i;
            break;
        }

        if (name[0] != '\0') {
            char lower_name[32] = {0};
            for (int j = 0; j < 31 && name[j]; j++) {
                lower_name[j] = tolower(name[j]);
            }
            if (strstr(lower_name, "xiaoxiang") != nullptr ||
                strstr(lower_name, "jbd") != nullptr ||
                strstr(lower_name, "bms") != nullptr) {
                Serial.printf("BMS BLE: Name match: '%s' (%s)\n", name, addr_str);
                match_idx = i;
                break;
            }
        }

        if (rssi > best_rssi) {
            best_rssi = rssi;
            best_idx = i;
        }
    }

    int target_idx = (match_idx >= 0) ? match_idx : best_idx;
    if (target_idx < 0) {
        Serial.println("BMS BLE: No BLE devices found");
        return false;
    }

    NimBLEAdvertisedDevice target = results.getDevice(target_idx);

    if (match_idx < 0) {
        Serial.printf("BMS BLE: No name/UUID match, trying strongest: %s (RSSI=%d)\n",
            target.getAddress().toString().c_str(), best_rssi);
    }

    char addr[18];
    strncpy(addr, target.getAddress().toString().c_str(), sizeof(addr) - 1);
    addr[sizeof(addr) - 1] = '\0';
    setBleAddress(addr);

    if (connectToDevice(_ble_address)) {
        savePreferredAddress(_ble_address);
        return true;
    }

    return false;
}

// ---- BLE Connection ----

// Direct connect by address — NimBLE does not require a prior scan
bool SerialBMS::connectToDevice(const char* ble_address) {
    setBleAddress(ble_address);

    Serial.printf("BMS BLE: Connecting to %s\n", _ble_address);

    // Create or reuse client
    if (_ble_client == nullptr) {
        _ble_client = NimBLEDevice::createClient();
        _ble_client->setClientCallbacks(&_client_callbacks, false);
        _ble_client->setConnectionParams(12, 12, 0, 200);  // fast connection
        _ble_client->setConnectTimeout(5);  // 5 seconds
    }

    NimBLEAddress addr(ble_address);

    unsigned long connect_start = millis();
    if (!_ble_client->connect(addr)) {
        Serial.printf("BMS BLE: Connect failed (%lums)\n", millis() - connect_start);
        _last_failure = millis();
        return false;
    }
    Serial.printf("BMS BLE: Connected (%lums)\n", millis() - connect_start);

    // Discover BMS service
    NimBLERemoteService* service = _ble_client->getService(BMS_SERVICE_UUID);
    if (service == nullptr) {
        Serial.println("BMS BLE: Service ff00 not found — not a BMS device");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }
    Serial.println("BMS BLE: Service ff00 found");

    // Get write characteristic (0xff02)
    _write_char = service->getCharacteristic(BMS_WRITE_UUID);
    if (_write_char == nullptr) {
        Serial.println("BMS BLE: Write char ff02 not found");
        // Debug: list all characteristics in service
        for (int i = 0; i < service->getCharacteristics()->size(); i++) {
            NimBLERemoteCharacteristic* c = (*service->getCharacteristics())[i];
            Serial.printf("BMS BLE:   char: %s\n",
                c->getUUID().toString().c_str());
        }
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }
    // JBD BMS uses write-no-response on ff02
    if (!_write_char->canWrite() && !_write_char->canWriteNoResponse()) {
        Serial.println("BMS BLE: ff02 not writable (props=0x%02X)");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    // Get notify characteristic (0xff01) and subscribe
    _notify_char = service->getCharacteristic(BMS_NOTIFY_UUID);
    if (_notify_char == nullptr || !_notify_char->canNotify()) {
        Serial.println("BMS BLE: Notify char ff01 not found or not subscribable");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    if (!_notify_char->subscribe(true, ble_notify_callback)) {
        Serial.println("BMS BLE: Failed to subscribe to ff01 notifications");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    _active_bms = this;
    _connected = true;
    _last_success = millis();
    Serial.printf("BMS BLE: Connected and ready (%s)\n", _ble_address);
    return true;
}

void SerialBMS::disconnect() {
    _connected = false;
    _write_char = nullptr;
    _notify_char = nullptr;
    if (_ble_client != nullptr && _ble_client->isConnected()) {
        _ble_client->disconnect();
    }
    if (_active_bms == this) {
        _active_bms = nullptr;
    }
}

bool SerialBMS::isConnected() {
    if (_ble_client == nullptr) {
        _connected = false;
        return false;
    }
    _connected = _ble_client->isConnected();
    return _connected;
}

// ---- Protocol: send command and wait for response ----

bool SerialBMS::sendCommand(const uint8_t* cmd, uint8_t cmd_len) {
    if (!isConnected() || _write_char == nullptr) {
        Serial.println("BMS BLE: Not connected, cannot send");
        return false;
    }

    // Clear receive buffer
    _rx_len = 0;
    _frame_ready = false;

    // Write command to BMS
    //Serial.printf("BMS BLE: TX %d bytes [%02X %02X ...]\n", cmd_len, cmd[0], cmd[1]);
    if (!_write_char->writeValue(cmd, cmd_len)) {
        Serial.println("BMS BLE: Write failed");
        return false;
    }
    return true;
}

bool SerialBMS::waitForFrame(unsigned long timeout_ms) {
    unsigned long start = millis();
    while (!_frame_ready && (millis() - start) < timeout_ms) {
        // NimBLE runs on its own FreeRTOS task — just yield
        delay(10);
    }
    return _frame_ready;
}

// ---- Decode: Basic Info (register 0x03) ----

bool SerialBMS::decodeBasicInfo(const uint8_t* buf, uint8_t len) {
    if (len < 10) return false;
    if (buf[0] != 0xDD || buf[len - 1] != 0x77) return false;
    if (buf[1] != 0x03) return false;

    uint8_t data_len = buf[3];
    if (len < (uint8_t)(data_len + 7)) return false;

    const uint8_t* d = &buf[4];

    _basic_info.voltage = ((d[0] << 8) | d[1]) * 0.01f;

    int16_t raw_current = (int16_t)((d[2] << 8) | d[3]);
    _basic_info.current = raw_current * 0.01f;

    _basic_info.remain_cap = ((d[4] << 8) | d[5]) * 0.01f;
    _basic_info.nominal_cap = ((d[6] << 8) | d[7]) * 0.01f;
    _basic_info.cycles = (d[8] << 8) | d[9];

    _basic_info.soc = (data_len > 19) ? d[19] : 0;
    _basic_info.mosfet_status = (data_len > 20) ? d[20] : 0;
    _basic_info.num_temps = (data_len > 22) ? d[22] : 0;

    if (_basic_info.num_temps > BMS_MAX_TEMPS) {
        _basic_info.num_temps = BMS_MAX_TEMPS;
    }

    for (uint8_t i = 0; i < _basic_info.num_temps; i++) {
        uint8_t offset = 23 + i * 2;
        if (offset + 1 < data_len) {
            uint16_t raw = (d[offset] << 8) | d[offset + 1];
            _basic_info.temps[i] = (raw - 2731) * 0.1f;
        }
    }

    return true;
}

// ---- Decode: Cell Voltages (register 0x04) ----

bool SerialBMS::decodeCellVoltages(const uint8_t* buf, uint8_t len) {
    if (len < 7) return false;
    if (buf[0] != 0xDD || buf[len - 1] != 0x77) return false;
    if (buf[1] != 0x04) return false;

    uint8_t data_len = buf[3];
    if (len < (uint8_t)(data_len + 7)) return false;

    const uint8_t* d = &buf[4];
    _cell_voltages.num_cells = 0;

    for (uint8_t i = 0; i + 1 < data_len && _cell_voltages.num_cells < BMS_MAX_CELLS; i += 2) {
        uint16_t mv = (d[i] << 8) | d[i + 1];
        _cell_voltages.cells[_cell_voltages.num_cells++] = mv * 0.001f;
    }

    return true;
}

// ---- BMS Query Commands ----

bool SerialBMS::queryBasicInfo() {
#if MOCK_DEBUG_BMS
    // Mock data for peak/off-peak testing — bypasses BLE entirely
    // SOC is set to 50% which sits between the default thresholds (LOW=5%, HIGH=80%), 
    // so the charge manager won't act on it — peak/off-peak transitions are the only 
    // thing that will fire. If you want to test the SOC threshold logic too, 
    // change _basic_info.soc to a value outside that range.
    _basic_info.voltage     = 52.80f;   // 16S LiFePO4 nominal
    _basic_info.current     = -5.00f;   // discharging
    _basic_info.remain_cap  = 50.00f;
    _basic_info.nominal_cap = 100.00f;
    _basic_info.cycles      = 42;
    _basic_info.soc         = 50;       // mid-range: won't trip SOC thresholds
    _basic_info.mosfet_status = 0x03;   // bit0=1, bit1=1 → charging allowed, discharging allowed
    _basic_info.num_temps   = 1;
    _basic_info.temps[0]    = 25.0f;
    Serial.println("BMS: [MOCK_DEBUG_BMS] Using mock basic info (SOC=50%)");
    _last_success = millis();
#else
    if (!sendCommand(CMD_BASIC_INFO, sizeof(CMD_BASIC_INFO))) return false;
    if (!waitForFrame(BMS_BLE_TIMEOUT)) {
        Serial.println("BMS: Basic info timeout");
        _last_failure = millis();
        return false;
    }

    if (!decodeBasicInfo(_rx_buf, _rx_len)) {
        Serial.println("BMS: Basic info decode failed");
        _last_failure = millis();
        return false;
    }

    _last_success = millis();
#endif

    // Status register bit convention (opposite of control commands):
    //   bit0=1 → charging enabled    bit0=0 → charging disabled
    //   bit1=1 → discharging enabled  bit1=0 → discharging disabled
    // (Control commands use inverse: CMD_MOSFET_CHG_OFF sends 0x01 to disable)
    bool charging_allowed    = (_basic_info.mosfet_status & 0x01);
    bool discharging_allowed = (_basic_info.mosfet_status & 0x02);

    Serial.printf("BMS: %.2fV %.3fA SOC=%d%% Cap=%.2f/%.2fAh Cycles=%d CHG=%s DSG=%s\n",
        _basic_info.voltage, _basic_info.current, _basic_info.soc,
        _basic_info.remain_cap, _basic_info.nominal_cap,
        _basic_info.cycles,
        charging_allowed    ? "allowed" : "disabled",
        discharging_allowed ? "allowed" : "disabled");

    for (uint8_t i = 0; i < _basic_info.num_temps; i++) {
        Serial.printf("BMS: Temp%d=%.1fC\n", i + 1, _basic_info.temps[i]);
    }

    // Publish to Homie MQTT — true = allowed, false = disabled
    _np_voltage.set_value_and_publish(_basic_info.voltage);
    _np_current.set_value_and_publish(_basic_info.current);
    _np_soc.set_value_and_publish((int)_basic_info.soc);
    _np_remain_cap.set_value_and_publish(_basic_info.remain_cap);
    _np_nominal_cap.set_value_and_publish(_basic_info.nominal_cap);
    _np_cycles.set_value_and_publish((int)_basic_info.cycles);

    _np_mosfet_charge.set_value_and_publish(charging_allowed    ? "true" : "false");
    _np_mosfet_discharge.set_value_and_publish(discharging_allowed ? "true" : "false");

    // First temperature sensor if available
    if (_basic_info.num_temps > 0) {
        _np_temperature.set_value_and_publish(_basic_info.temps[0]);
    }

    return true;
}

bool SerialBMS::queryCellVoltages() {
#if MOCK_DEBUG_BMS
    // Mock 16-cell pack at nominal LiFePO4 voltages with slight variation
    _cell_voltages.num_cells = 16;
    static const float mock_cells[16] = {
        3.301f, 3.298f, 3.302f, 3.299f, 3.300f, 3.301f, 3.298f, 3.297f,
        3.300f, 3.302f, 3.299f, 3.301f, 3.298f, 3.300f, 3.299f, 3.301f
    };
    for (uint8_t i = 0; i < 16; i++) {
        _cell_voltages.cells[i] = mock_cells[i];
    }
    Serial.println("BMS: [DEBUG] Using mock cell voltages (16 cells ~3.300V)");
    _last_success = millis();
#else
    if (!sendCommand(CMD_CELL_VOLTAGES, sizeof(CMD_CELL_VOLTAGES))) return false;
    if (!waitForFrame(BMS_BLE_TIMEOUT)) {
        Serial.println("BMS: Cell voltages timeout");
        _last_failure = millis();
        return false;
    }

    if (!decodeCellVoltages(_rx_buf, _rx_len)) {
        Serial.println("BMS: Cell voltages decode failed");
        _last_failure = millis();
        return false;
    }

    _last_success = millis();
#endif

    // Publish cell voltages as JSON directly to MQTT (too large for NodeProperty's 32-byte buffer)
    // Format: {"count":16,"cells":[3.379,3.378,...],"min":3.371,"max":3.381,"delta":0.010}
    static char json[512];
    int pos = 0;
    float min_v = 99.0f, max_v = 0.0f;

    pos += snprintf(&json[pos], sizeof(json) - pos, "{\"count\":%d,\"cells\":[", _cell_voltages.num_cells);

    for (uint8_t i = 0; i < _cell_voltages.num_cells && pos < (int)sizeof(json) - 32; i++) {
        float v = _cell_voltages.cells[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        if (i > 0) json[pos++] = ',';
        pos += snprintf(&json[pos], sizeof(json) - pos, "%.3f", v);
        Serial.printf("BMS: Cell %d: %.3fV\n", i + 1, v);
    }

    pos += snprintf(&json[pos], sizeof(json) - pos,
        "],\"min\":%.3f,\"max\":%.3f,\"delta\":%.3f}",
        min_v, max_v, max_v - min_v);

    // Publish directly — JSON exceeds mqtt_queue 64-byte payload limit
    // Safe: BMS loop runs on core 1 same as MQTT client
    mqtt_client.publish(_np_cell_voltages.topic(), json);

    return true;
}

// ---- MOSFET Control ----
// Update mosfet_status optimistically so charge_manager guard works until next poll.
bool SerialBMS::setMosfetAllOn() {
    bool ok = MOCK_DEBUG_BMS ? true : sendCommand(CMD_MOSFET_ALL_ON, sizeof(CMD_MOSFET_ALL_ON));
    if (ok) _basic_info.mosfet_status |= 0x03;
    return ok;
}

bool SerialBMS::setMosfetChgOff() {
    bool ok = MOCK_DEBUG_BMS ? true : sendCommand(CMD_MOSFET_CHG_OFF, sizeof(CMD_MOSFET_CHG_OFF));
    if (ok) _basic_info.mosfet_status &= ~0x01;
    return ok;
}

bool SerialBMS::setMosfetDsgOff() {
    bool ok = MOCK_DEBUG_BMS ? true : sendCommand(CMD_MOSFET_DSG_OFF, sizeof(CMD_MOSFET_DSG_OFF));
    if (ok) _basic_info.mosfet_status &= ~0x02;
    return ok;
}

bool SerialBMS::setMosfetAllOff() {
    bool ok = MOCK_DEBUG_BMS ? true : sendCommand(CMD_MOSFET_ALL_OFF, sizeof(CMD_MOSFET_ALL_OFF));
    if (ok) _basic_info.mosfet_status = 0x00;
    return ok;
}

// ---- Peak-hour scheduling ----

// Returns true if current local time falls inside the configured peak window.
// Returns false if time is unavailable (NTP not yet synced) — safe default is off-peak.
bool SerialBMS::isPeakTime() {
    struct tm t;
    if (!getLocalTime(&t, 0)) {
        return false;  // NTP not synced yet — treat as off-peak
    }
    int peak_start = atoi(_np_peak_begin_min.value());
    int peak_end   = atoi(_np_peak_end_min.value());
    if (peak_start <= 0 || peak_end  <= 0) return false;
    int minutes_of_day = t.tm_hour * 60 + t.tm_min;
    return (minutes_of_day >= peak_start && minutes_of_day < peak_end);
}

// ---- Poll Interval ----

bool SerialBMS::pollIntervalTrigger() {
    if (millis() - _poll_timestamp > (unsigned long)_poll_interval) {
        _poll_timestamp = millis();
        return true;
    }
    return false;
}

void SerialBMS::charge_manager() {
    int LOW_THRESHOLD = atoi(_np_charge_threshold_low.value());
    int HIGH_THRESHOLD = atoi(_np_charge_threshold_high.value());
    if (LOW_THRESHOLD <= 0) LOW_THRESHOLD = 30;
    if (HIGH_THRESHOLD <= 0) HIGH_THRESHOLD = 80;

    bool peak = isPeakTime();

    // --- SSR (AC contactor): transitions only at peak/off-peak boundary ---
    if (!_peak_state_known || peak != _last_peak_state) {
        if (peak) {
            // Entering peak: MOSFET off first, then open contactor
            Serial.println("BMS: Entering peak period - disabling charge MOSFET then contactor");
            setMosfetChgOff();
            delay(1000);
            if (_ssr) _ssr->set(false);
        } else {
            // Entering off-peak
            bool threshold_discharge = (strcmp(_np_threshold_discharge_daily.value(), "true") == 0);
            if (threshold_discharge && _basic_info.soc > LOW_THRESHOLD) {
                // threshold_discharge_daily: hold off — discharge to LOW before charging
                Serial.printf("BMS: Entering off-peak - range discharge mode, SOC=%d%% > LOW=%d%%, holding charge/SSR\n",
                    _basic_info.soc, LOW_THRESHOLD);
                _in_discharge_phase = true;
            } else {
                _in_discharge_phase = false;
                Serial.println("BMS: Entering off-peak period - enabling contactor");
                if (_ssr) _ssr->set(true);
                if (_basic_info.soc < HIGH_THRESHOLD) {
                    Serial.printf("BMS: Entering off-peak - enabling charge MOSFET (SOC=%d%% < HIGH=%d%%)\n",
                        _basic_info.soc, HIGH_THRESHOLD);
                    if (!setMosfetAllOn()) {
                        Serial.println("BMS: Failed to enable charge MOSFET on off-peak entry");
                    }
                } else {
                    Serial.printf("BMS: Entering off-peak - charge MOSFET stays off (SOC=%d%% >= HIGH=%d%%)\n",
                        _basic_info.soc, HIGH_THRESHOLD);
                }
            }
        }
        _last_peak_state = peak;
        _peak_state_known = true;
    }

    // --- CHG MOSFET: SOC threshold control, off-peak only ---
    if (!peak) {
        Serial.printf("BMS: Off-peak charge manager: SOC=%d%%, LOW=%d%% HIGH=%d%%%s\n",
            _basic_info.soc, LOW_THRESHOLD, HIGH_THRESHOLD,
            _in_discharge_phase ? " [discharge phase]" : "");

        // threshold_discharge_daily: wait until SOC drops to LOW before enabling charge
        if (_in_discharge_phase) {
            if (_basic_info.soc <= LOW_THRESHOLD) {
                Serial.printf("BMS: Discharge phase complete, SOC=%d%% <= LOW=%d%% - enabling contactor and charge MOSFET\n",
                    _basic_info.soc, LOW_THRESHOLD);
                _in_discharge_phase = false;
                if (_ssr) _ssr->set(true);
                delay(1000);
                if (!setMosfetAllOn()) {
                    Serial.println("BMS: Failed to enable charge MOSFET after discharge phase");
                }
            } else {
                Serial.printf("BMS: Discharge phase: SOC=%d%% > LOW=%d%%, holding charge off\n",
                    _basic_info.soc, LOW_THRESHOLD);
            }
            return;
        }

        bool chg_enabled = (_basic_info.mosfet_status & 0x01);  // bit0=1 means charging enabled
        if (_basic_info.soc >= HIGH_THRESHOLD) {
            if (chg_enabled) {
                Serial.printf("BMS: SOC at/above high threshold (%d%%) - disabling charge MOSFET\n", HIGH_THRESHOLD);
                if (!setMosfetChgOff()) {
                    Serial.println("BMS: Failed to disable charge MOSFET");
                }
            }
        } else if (!chg_enabled) {
            if (_ssr && !_ssr->getState()) {
                Serial.println("BMS: Contactor open - closing before enabling charge MOSFET");
                _ssr->set(true);
                delay(1000);
            }
            Serial.printf("BMS: SOC=%d%% below HIGH=%d%% - enabling charge MOSFET\n",
            _basic_info.soc, HIGH_THRESHOLD);
            if (!setMosfetAllOn()) {
                Serial.println("BMS: Failed to enable charge MOSFET");
            }
        }
        
    } else {
        Serial.println("BMS: Peak period - charge MOSFET control disabled");
    }

}
// ---- Main Loop ----

void SerialBMS::loop() {
#if !MOCK_DEBUG_BMS
    if (!_ble_initialized) {
        static unsigned long last_noinit_log = 0;
        if (millis() - last_noinit_log > 60000) {
            Serial.println("BMS BLE: Not initialized, skipping loop");
            last_noinit_log = millis();
        }
        return;
    }

    // If not connected, periodically attempt reconnect
    if (!isConnected()) {
        if (_connected) {
            Serial.printf("BMS BLE: Connection lost to %s\n", _ble_address);
            _connected = false;
        }
        unsigned long since_last = millis() - _last_reconnect_attempt;
        if (since_last > BMS_RECONNECT_INTERVAL) {
            Serial.printf("BMS BLE: Reconnecting (last attempt %lus ago)...\n",
                since_last / 1000);
            scanAndConnect();
        }
        return;
    }
#endif

    if (pollIntervalTrigger()) {
        struct tm t;
        if (getLocalTime(&t, 0)) {
            Serial.printf("BMS BLE: Time now: [%04d-%02d-%02d %02d:%02d:%02d]\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            Serial.printf("BMS BLE: Polling (last OK %lus ago)\n", (millis() - _last_success) / 1000);
        }
        queryCellVoltages();
        if (queryBasicInfo()) {
            Serial.println("BMS BLE: Poll successful");
            charge_manager();
        } else {
            Serial.println("BMS BLE: Poll failed");

        }
    }
}

// ---- Settable: charge_control ----

void SerialBMS::set(bool onOff) {
    Serial.printf("BMS: Charge control SET: %s\n", onOff ? "ON" : "OFF");
    if (onOff) {
        //energize the AC contactor before re-enabling the charge MOSFETs
        if (_ssr) _ssr->set(true);
        delay(1500);
        setMosfetAllOn();
    } else {
        setMosfetChgOff();
        delay(1500);
        //de-energize the AC contactor after disabling the charge MOSFETs
        if (_ssr) _ssr->set(false);
    }
    _np_charge_control.set_value_and_publish(onOff);
}

void SerialBMS::entity_settable_callback(Property* property) {
    Serial.printf("BMS: settable callback(Property*) '%s'='%s'\n", property->id(), property->value());
    entity_settable_callback(property->id(), property->value());
}

void SerialBMS::entity_settable_callback(const char* property_id, const char* value) {
    Serial.printf("BMS: settable callback '%s'='%s'\n", property_id, value);

    if (strcmp(property_id, "rescan") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            Serial.println("BMS: Rescan requested via MQTT — clearing preferred address");
            clearPreferredAddress();
            disconnect();
            _last_reconnect_attempt = 0;  // loop() will call scanAndConnect() immediately
            _np_rescan.set_value_and_publish(false);  // reset — it's a trigger, not state
        }
    } else if (strcmp(property_id, "mosfet_charge") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            Serial.println("BMS: mosfet_charge SET true");
            setMosfetAllOn();
            _np_mosfet_charge.set_value_and_publish("true");
            _np_mosfet_discharge.set_value_and_publish("true");
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
            Serial.println("BMS: mosfet_charge SET false");
            setMosfetChgOff();
            _np_mosfet_charge.set_value_and_publish("false");
        } else {
            Serial.printf("BMS: invalid value '%s' for mosfet_charge (expected true/false)\n", value);
        }
    } else if (strcmp(property_id, "charge_control") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
            strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) {
            set(true);
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                   strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
            set(false);
        } else {
            Serial.printf("BMS: invalid value '%s' for charge_control\n", value);
        }
    } else if (strcmp(property_id, "charge_threshold_low") == 0) {
        int threshold = atoi(value);
        if (threshold > 0 && threshold < 100) {
            Serial.printf("BMS: Setting charge low-threshold to %d%%\n", threshold);
            _np_charge_threshold_low.set_value_and_publish(threshold);
        } else {
            Serial.printf("BMS: Invalid charge_threshold_low value '%s'\n", value);
        }
    } else if (strcmp(property_id, "charge_threshold_high") == 0) {
        int threshold = atoi(value);
        if (threshold > 0 && threshold < 100) {
            Serial.printf("BMS: Setting charge high-threshold to %d%%\n", threshold);
            _np_charge_threshold_high.set_value_and_publish(threshold);
        } else {
            Serial.printf("BMS: Invalid charge_threshold_high value '%s'\n", value);
        }
    } else if (strcmp(property_id, "peak_begin_min") == 0) {
        int minutes = atoi(value);
        if (minutes > 0 && minutes < 1440) {
            Serial.printf("BMS: Setting peak start to %d min (%02d:%02d)\n",
                minutes, minutes / 60, minutes % 60);
            _np_peak_begin_min.set_value_and_publish(minutes);
        } else {
            Serial.printf("BMS: Invalid peak_begin_min value '%s'\n", value);
        }
    } else if (strcmp(property_id, "peak_end_min") == 0) {
        int minutes = atoi(value);
        if (minutes > 0 && minutes < 1440) {
            Serial.printf("BMS: Setting peak end to %d min (%02d:%02d)\n",
                minutes, minutes / 60, minutes % 60);
            _np_peak_end_min.set_value_and_publish(minutes);
        } else {
            Serial.printf("BMS: Invalid peak_end_min value '%s'\n", value);
        }
    } else if (strcmp(property_id, "threshold_discharge_daily") == 0) {
        bool enable = (strcmp(value, "true") == 0);
        Serial.printf("BMS: threshold_discharge_daily -> %s\n", enable ? "true" : "false");
        _np_threshold_discharge_daily.set_value_and_publish(enable ? "true" : "false");
        if (!enable) {
            _in_discharge_phase = false;  // cancel any active discharge wait
        }
    }
}

