#include <Arduino.h>
#include <config.h>
#include <node/ble/serial_bms.h>
#include <radio/radio_broker.h>
#include <network/mqtt_client.h>
#include <Preferences.h>

// NimBLE — lightweight BLE stack, supports direct connect by address
#include <NimBLEDevice.h>

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

    // Register BT radio need — resolved by radio_broker before setup()
    radio_request(RADIO_BT, "battery_bms");
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
    _np_mosfet.setup(getNodeId(), "mosfet_status");
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

    // Check if BT radio was granted by broker
    if (radio_query(RADIO_BT) != RADIO_GRANT_ALLOWED) {
        Serial.println("BMS BLE: BT radio not granted, BLE disabled");
        _ble_initialized = false;
        return true;  // graceful degradation — node exists but won't poll
    }

    Serial.printf("BMS BLE: Free heap before init: %u bytes\n", ESP.getFreeHeap());

    // Initialize NimBLE
    NimBLEDevice::init("ESP32_BMS");
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

    const char* chg = (_basic_info.mosfet_status & 0x01) ? "OFF" : "ON";
    const char* dsg = (_basic_info.mosfet_status & 0x02) ? "OFF" : "ON";

    Serial.printf("BMS: %.2fV %.3fA SOC=%d%% Cap=%.2f/%.2fAh Cycles=%d CHG=%s DSG=%s\n",
        _basic_info.voltage, _basic_info.current, _basic_info.soc,
        _basic_info.remain_cap, _basic_info.nominal_cap,
        _basic_info.cycles, chg, dsg);

    for (uint8_t i = 0; i < _basic_info.num_temps; i++) {
        Serial.printf("BMS: Temp%d=%.1fC\n", i + 1, _basic_info.temps[i]);
    }

    // Publish to Homie MQTT
    _np_voltage.set_value_and_publish(_basic_info.voltage);
    _np_current.set_value_and_publish(_basic_info.current);
    _np_soc.set_value_and_publish((int)_basic_info.soc);
    _np_remain_cap.set_value_and_publish(_basic_info.remain_cap);
    _np_nominal_cap.set_value_and_publish(_basic_info.nominal_cap);
    _np_cycles.set_value_and_publish((int)_basic_info.cycles);

    // MOSFET status as string
    char mosfet_str[16];
    snprintf(mosfet_str, sizeof(mosfet_str), "CHG=%s,DSG=%s", chg, dsg);
    _np_mosfet.set_value_and_publish(mosfet_str);

    // First temperature sensor if available
    if (_basic_info.num_temps > 0) {
        _np_temperature.set_value_and_publish(_basic_info.temps[0]);
    }

    return true;
}

bool SerialBMS::queryCellVoltages() {
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

bool SerialBMS::setMosfetAllOn() {
    return sendCommand(CMD_MOSFET_ALL_ON, sizeof(CMD_MOSFET_ALL_ON));
}

bool SerialBMS::setMosfetChgOff() {
    return sendCommand(CMD_MOSFET_CHG_OFF, sizeof(CMD_MOSFET_CHG_OFF));
}

bool SerialBMS::setMosfetDsgOff() {
    return sendCommand(CMD_MOSFET_DSG_OFF, sizeof(CMD_MOSFET_DSG_OFF));
}

bool SerialBMS::setMosfetAllOff() {
    return sendCommand(CMD_MOSFET_ALL_OFF, sizeof(CMD_MOSFET_ALL_OFF));
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
    // Simple charge control logic: if SOC < 20% and MOSFET is off, turn it on. If SOC > 80% and MOSFET is on, turn it off.
    //bool chg_off = (_basic_info.mosfet_status & 0x01) != 0;
    int LOW_THRESHOLD = atoi(_np_charge_threshold_low.value());
    int HIGH_THRESHOLD = atoi(_np_charge_threshold_high.value());
    //sanity - set defaults if thresholds not defined or invalid
    if (LOW_THRESHOLD <= 0) LOW_THRESHOLD = 5;
    if (HIGH_THRESHOLD <= 0) HIGH_THRESHOLD = 80;
    Serial.printf("BMS: Charge manager check: SOC=%d%%, thresholds: LOW=%d%% HIGH=%d%%\n",
        _basic_info.soc, LOW_THRESHOLD, HIGH_THRESHOLD);
    if (_basic_info.soc < LOW_THRESHOLD) {
        if (_ssr) _ssr->set(true); //energize the contactor before enabling the charge MOSFETs
        delay(1000);
        Serial.printf("*** \nBMS: SOC low threshold reached: %d%%, enabling charge MOSFET", LOW_THRESHOLD);
        if (setMosfetAllOn()) {
            Serial.println("BMS: Charge MOSFET enabled - starting charge");
        } else {
            Serial.println("BMS: Failed to enable charge MOSFET");
        }
    } else if (_basic_info.soc > HIGH_THRESHOLD) {
        Serial.printf("*** \nBMS: SOC high threshold reached: %d%%, disabling charge MOSFET", HIGH_THRESHOLD);
        if (setMosfetChgOff()) { //disable charge MOSFET before de-energizing the contactor
            Serial.println("BMS: Charge MOSFET disabled - stopping charge");
        } else {
            Serial.println("BMS: Failed to disable charge MOSFET");
        }
        delay(1000);
        if (_ssr) _ssr->set(false);
    }
}
// ---- Main Loop ----

void SerialBMS::loop() {
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

    if (pollIntervalTrigger()) {
        Serial.printf("BMS BLE: Polling %s (last OK %lus ago)\n",
            _ble_address, (millis() - _last_success) / 1000);
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
        delay(3000);
        setMosfetAllOn();
    } else {
        setMosfetChgOff();
        delay(3000);
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
    }
}

