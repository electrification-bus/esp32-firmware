#include <Arduino.h>
#include <config.h>
#include <node/bt/serial_bms.h>

// ESP32 Arduino BLE client
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>

// JBD BMS BLE GATT UUIDs
static BLEUUID BMS_SERVICE_UUID("0000ff00-0000-1000-8000-00805f9b34fb");
static BLEUUID BMS_NOTIFY_UUID("0000ff01-0000-1000-8000-00805f9b34fb");
static BLEUUID BMS_WRITE_UUID("0000ff02-0000-1000-8000-00805f9b34fb");

// JBD BMS protocol commands (from BmsProtocol.kt)
// Frame: 0xDD, R/W, reg, len, data..., checksum_hi, checksum_lo, 0x77
static const uint8_t CMD_BASIC_INFO[]    = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
static const uint8_t CMD_CELL_VOLTAGES[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};
// static const uint8_t CMD_HARDWARE_VER[]  = {0xDD, 0xA5, 0x05, 0x00, 0xFF, 0xFB, 0x77};

static const uint8_t CMD_MOSFET_ALL_ON[]  = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x00, 0xFF, 0x1D, 0x77};
static const uint8_t CMD_MOSFET_CHG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x01, 0xFF, 0x1C, 0x77};
static const uint8_t CMD_MOSFET_DSG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x02, 0xFF, 0x1B, 0x77};
static const uint8_t CMD_MOSFET_ALL_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x03, 0xFF, 0x1A, 0x77};

// Singleton pointer for BLE callback context (one BMS connection at a time)
static SerialBMS* _active_bms = nullptr;

// BLE client and characteristic pointers (static, no dynamic alloc beyond BLE library internals)
static BLEClient* _ble_client = nullptr;
static BLERemoteCharacteristic* _write_char = nullptr;
static BLERemoteCharacteristic* _notify_char = nullptr;


// ---- Static callback trampoline ----

void SerialBMS::notifyCallback(void* characteristic, uint8_t* data, size_t length, bool isNotify) {
    if (_active_bms == nullptr) return;

    SerialBMS* bms = _active_bms;

    for (size_t i = 0; i < length; i++) {
        if (bms->_rx_len < BMS_FRAME_MAX) {
            bms->_rx_buf[bms->_rx_len++] = data[i];
        }
    }

    // Check for complete frame: starts with 0xDD, ends with 0x77
    if (bms->_rx_len >= 7 &&
        bms->_rx_buf[0] == 0xDD &&
        bms->_rx_buf[bms->_rx_len - 1] == 0x77) {
        bms->_frame_ready = true;
    }
}
// BLE notification callback - accumulates frame data
static void ble_notify_callback(
    BLERemoteCharacteristic* pChar,
    uint8_t* pData,
    size_t length,
    bool isNotify
) {
    if (_active_bms == nullptr) return;

    // Forward to instance method via the static trampoline
    SerialBMS::notifyCallback(pChar, pData, length, isNotify);
}

// ---- BLE client callbacks ----

class BmsClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        Serial.println("BMS BLE: Connected");
    }
    void onDisconnect(BLEClient* pClient) override {
        Serial.println("BMS BLE: Disconnected");
        if (_active_bms) {
            _active_bms->disconnect();
        }
    }
};

static BmsClientCallbacks _client_callbacks;

// ---- Constructor ----

SerialBMS::SerialBMS(const char* homie_node_id) : NodeEntity(homie_node_id) {
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
}

void SerialBMS::setBleAddress(const char* address) {
    strncpy(_ble_address, address, sizeof(_ble_address) - 1);
    _ble_address[sizeof(_ble_address) - 1] = '\0';
}

// ---- Setup ----

bool SerialBMS::setup() {
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

    // Initialize BLE
    BLEDevice::init("ESP32_BMS");
    _poll_timestamp = millis();

    // Auto-connect if address was set
    if (_ble_address[0] != '\0') {
        Serial.printf("BMS BLE: Auto-connecting to %s\n", _ble_address);
        connectToDevice(_ble_address);
    }

    return true;
}

// ---- BLE Connection ----

bool SerialBMS::connectToDevice(const char* ble_address) {
    setBleAddress(ble_address);

    Serial.printf("BMS BLE: Connecting to %s\n", _ble_address);

    BLEAddress addr(ble_address);

    // Create client (BLE library manages allocation internally)
    _ble_client = BLEDevice::createClient();
    _ble_client->setClientCallbacks(&_client_callbacks);

    if (!_ble_client->connect(addr)) {
        Serial.println("BMS BLE: Connection failed");
        _last_failure = millis();
        return false;
    }

    // Discover BMS service
    BLERemoteService* service = _ble_client->getService(BMS_SERVICE_UUID);
    if (service == nullptr) {
        Serial.println("BMS BLE: BMS service not found");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    // Get write characteristic (0xff02)
    _write_char = service->getCharacteristic(BMS_WRITE_UUID);
    if (_write_char == nullptr) {
        Serial.println("BMS BLE: Write characteristic not found");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    // Get notify characteristic (0xff01) and register for notifications
    _notify_char = service->getCharacteristic(BMS_NOTIFY_UUID);
    if (_notify_char == nullptr) {
        Serial.println("BMS BLE: Notify characteristic not found");
        _ble_client->disconnect();
        _last_failure = millis();
        return false;
    }

    _active_bms = this;
    _notify_char->registerForNotify(ble_notify_callback);

    _connected = true;
    _last_success = millis();
    Serial.println("BMS BLE: Connected and ready");
    return true;
}

void SerialBMS::disconnect() {
    if (_ble_client != nullptr && _ble_client->isConnected()) {
        _ble_client->disconnect();
    }
    _connected = false;
    _write_char = nullptr;
    _notify_char = nullptr;
    if (_active_bms == this) {
        _active_bms = nullptr;
    }
}

bool SerialBMS::isConnected() {
    if (_ble_client == nullptr) return false;
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
    _write_char->writeValue((uint8_t*)cmd, cmd_len);
    return true;
}

bool SerialBMS::waitForFrame(unsigned long timeout_ms) {
    unsigned long start = millis();
    while (!_frame_ready && (millis() - start) < timeout_ms) {
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

    // Build cell voltage string for publishing
    char cell_str[128];
    int pos = 0;
    for (uint8_t i = 0; i < _cell_voltages.num_cells && pos < (int)sizeof(cell_str) - 8; i++) {
        if (i > 0) {
            cell_str[pos++] = ',';
        }
        pos += snprintf(&cell_str[pos], sizeof(cell_str) - pos, "%.3f", _cell_voltages.cells[i]);
        Serial.printf("BMS: Cell %d: %.3fV\n", i + 1, _cell_voltages.cells[i]);
    }
    cell_str[pos] = '\0';

    _np_cell_voltages.set_value_and_publish(cell_str);

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

// ---- Main Loop ----

void SerialBMS::loop() {
    if (!isConnected()) return;

    if (pollIntervalTrigger()) {
        queryBasicInfo();
        queryCellVoltages();
    }
}