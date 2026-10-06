#include <Arduino.h>
#include <time.h>
#include <platform/mqtt_client.h>
#include "serial_bms.h"
#include <homie/Property.h>   // entity_settable_callback(Property*) dereferences it

// All NimBLE handling now lives in lib/ble/ble_client.* — this file contains only the
// JBD protocol and the charging policy. That split is the whole point of the example:
// compare it with lib/ble/gatt_device.cpp, which uses the identical transport and knows
// no protocol at all.

#define MOCK_DEBUG_BMS false

// JBD BMS BLE GATT UUIDs
#define BMS_SERVICE_UUID "0000ff00-0000-1000-8000-00805f9b34fb"
#define BMS_NOTIFY_UUID  "0000ff01-0000-1000-8000-00805f9b34fb"
#define BMS_WRITE_UUID   "0000ff02-0000-1000-8000-00805f9b34fb"

// JBD BMS protocol commands (from BmsProtocol.kt)
// Frame: 0xDD, R/W, reg, len, data..., checksum_hi, checksum_lo, 0x77
static const uint8_t CMD_BASIC_INFO[]    = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
static const uint8_t CMD_CELL_VOLTAGES[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};

static const uint8_t CMD_MOSFET_ALL_ON[]  = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x00, 0xFF, 0x1D, 0x77};
static const uint8_t CMD_MOSFET_CHG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x01, 0xFF, 0x1C, 0x77};
static const uint8_t CMD_MOSFET_DSG_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x02, 0xFF, 0x1B, 0x77};
static const uint8_t CMD_MOSFET_ALL_OFF[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x03, 0xFF, 0x1A, 0x77};

// ---- Constructor ----

SerialBMS::SerialBMS(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _ssr = nullptr;
    _peak_state_known    = false;
    _last_peak_state     = false;
    _in_discharge_phase  = false;
    _rx_len = 0;
    _frame_ready = false;
    memset(&_basic_info, 0, sizeof(_basic_info));
    memset(&_cell_voltages, 0, sizeof(_cell_voltages));
    _poll_timestamp = 0;
    _last_success = 0;
    _last_failure = 0;
    _poll_interval = BMS_POLL_INTERVAL;
}

// ---- BLE receive ----

void SerialBMS::on_ble_rx(void* ctx, const uint8_t* data, size_t len) {
    static_cast<SerialBMS*>(ctx)->handleRx(data, len);
}

// Runs on the NimBLE task. Accumulate until a complete frame is present, then flag it —
// queryBasicInfo()/queryCellVoltages() are blocked in waitForFrame() on the Arduino task.
void SerialBMS::handleRx(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Read-modify-write spelled out: ++ on a volatile is deprecated in C++20 (GCC 12
        // warns). Only this task writes _rx_len, so nothing is lost by splitting it.
        uint8_t n = _rx_len;
        if (n < BMS_FRAME_MAX) { _rx_buf[n] = data[i]; _rx_len = n + 1; }
    }
    if (_rx_len >= 7 && _rx_buf[0] == 0xDD && _rx_buf[_rx_len - 1] == 0x77) {
        Serial.printf("BMS BLE: frame complete, %d bytes\n", _rx_len);
        _frame_ready = true;
    }
}

// ---- Setup ----

bool SerialBMS::setup() {
    Serial.println("=== BMS BLE SETUP START ===");
    NodeEntity::setup();

    _np_voltage.setup(getNodeId(), "voltage");
    _np_current.setup(getNodeId(), "current");
    _np_soc.setup(getNodeId(), "soc");
    _np_remain_cap.setup(getNodeId(), "remain-capacity");
    _np_nominal_cap.setup(getNodeId(), "nominal-capacity");
    _np_cycles.setup(getNodeId(), "cycles");
    _np_mosfet_charge.setup(getNodeId(), "mosfet-charge",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_mosfet_discharge.setup(getNodeId(), "mosfet-discharge");
    _np_temperature.setup(getNodeId(), "temperature");
    _np_cell_voltages.setup(getNodeId(), "cell-voltages");

    // settables
    _np_charge_control.setup(getNodeId(), "charge-control",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_rescan.setup(getNodeId(), "rescan",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_charge_threshold_low.setup(getNodeId(), "charge-threshold-low",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_charge_threshold_high.setup(getNodeId(), "charge-threshold-high",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_peak_begin_min.setup(getNodeId(), "peak-begin-min",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_peak_end_min.setup(getNodeId(), "peak-end-min",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _np_threshold_discharge_daily.setup(getNodeId(), "threshold-discharge-daily",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);

    // Publish initial values of settable properties so the broker has retained state
    _np_charge_threshold_low.publish();
    _np_charge_threshold_high.publish();
    _np_peak_begin_min.publish();
    _np_peak_end_min.publish();
    _np_threshold_discharge_daily.publish();

    // Transport: UUIDs + a name filter for BMSes that do not advertise their service.
    // Everything else (scan, connect, reconnect, NVS address cache) is the link's job.
    _link.setTag("BMS BLE");
    _link.setUUIDs(BMS_SERVICE_UUID, BMS_NOTIFY_UUID, BMS_WRITE_UUID);
    _link.setNameFilter("xiaoxiang,jbd,bms");
    _link.setNVSNamespace("bms_ble");
    _link.onNotify(&SerialBMS::on_ble_rx, this);
    _link.begin("ESP32_BMS");

    _poll_timestamp = millis();
    Serial.println("=== BMS BLE SETUP COMPLETE ===");
    return true;
}

// ---- Protocol: send command and wait for response ----

bool SerialBMS::sendCommand(const uint8_t* cmd, uint8_t cmd_len) {
    if (!_link.isConnected()) {
        Serial.println("BMS BLE: not connected, cannot send");
        return false;
    }
    _rx_len = 0;
    _frame_ready = false;
    if (!_link.write(cmd, cmd_len)) {
        Serial.println("BMS BLE: write failed");
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

    // Through the property like everything else. This used to be a direct
    // mqtt_client.publish() because the queue truncated at 64 bytes — it no longer does
    // (MQTT_PAYLOAD_MAX is 256, and over-long payloads are now reported rather than
    // silently clipped). The direct call also published NON-retained, since the two-arg
    // publish() defaults retained=false, which contradicted the property's declaration.
    _np_cell_voltages.set_value_and_publish(json);

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

    // --- AC contactor (gpio/digital_output): transitions only at the peak boundary ---
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
    // Reconnect backoff is the link's responsibility now.
    _link.loop();
    if (!_link.isConnected()) return;
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
    setChargeControl(onOff);
}

// Returns false, publishing nothing, when the MOSFET command does not reach the BMS.
// The contactor is meant to switch with the charge MOSFET off: ON closes it before enabling
// the MOSFETs, OFF opens it after disabling the charge MOSFET. On a refusal OFF leaves
// the contactor alone, and ON opens it again if it was open before. The exception: if
// _basic_info records the charge MOSFET as on, ON leaves the contactor closed rather than
// open it under charge. A failed command does not update _basic_info.
bool SerialBMS::setChargeControl(bool onOff) {
    Serial.printf("BMS: Charge control SET: %s\n", onOff ? "ON" : "OFF");
    if (onOff) {
        bool contactor_was_on = _ssr ? _ssr->getState() : false;
        //energize the AC contactor before re-enabling the charge MOSFETs
        if (_ssr) _ssr->set(true);
        delay(1500);
        if (!setMosfetAllOn()) {
            if (_ssr && !contactor_was_on && !(_basic_info.mosfet_status & 0x01)) {
                Serial.println("BMS: charge MOSFET not enabled - opening the contactor again");
                _ssr->set(false);
            }
            return false;
        }
    } else {
        if (!setMosfetChgOff()) {
            Serial.println("BMS: charge MOSFET not disabled - contactor left as it was");
            return false;
        }
        delay(1500);
        //de-energize the AC contactor after disabling the charge MOSFETs
        if (_ssr) _ssr->set(false);
    }
    _np_charge_control.set_value_and_publish(onOff);
    return true;
}

bool SerialBMS::entity_settable_callback(Property* property) {
    Serial.printf("BMS: settable callback(Property*) '%s'='%s'\n", property->id(), property->value());
    return entity_settable_callback(property->id(), property->value());
}

bool SerialBMS::entity_settable_callback(const char* property_id, const char* value) {
    Serial.printf("BMS: settable callback '%s'='%s'\n", property_id, value);

    if (strcmp(property_id, "rescan") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            Serial.println("BMS: Rescan requested via MQTT — clearing cached address");
            // The link drops the NVS entry, disconnects, and rescans on the next loop().
            _link.forgetAddress();
            _np_rescan.set_value_and_publish(false);  // reset — it's a trigger, not state
        }
        return true;
    } else if (strcmp(property_id, "mosfet-charge") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            Serial.println("BMS: mosfet_charge SET true");
            if (!setMosfetAllOn()) return false;
            _np_mosfet_charge.set_value_and_publish("true");
            _np_mosfet_discharge.set_value_and_publish("true");
            return true;
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
            Serial.println("BMS: mosfet_charge SET false");
            if (!setMosfetChgOff()) return false;
            _np_mosfet_charge.set_value_and_publish("false");
            return true;
        }
        Serial.printf("BMS: invalid value '%s' for mosfet_charge (expected true/false)\n", value);
        return false;
    } else if (strcmp(property_id, "charge-control") == 0) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
            strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) {
            return setChargeControl(true);
        } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                   strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
            return setChargeControl(false);
        }
        Serial.printf("BMS: invalid value '%s' for charge_control\n", value);
        return false;
    } else if (strcmp(property_id, "charge-threshold-low") == 0) {
        int threshold = atoi(value);
        if (threshold > 0 && threshold < 100) {
            Serial.printf("BMS: Setting charge low-threshold to %d%%\n", threshold);
            _np_charge_threshold_low.set_value_and_publish(threshold);
            return true;
        }
        Serial.printf("BMS: Invalid charge_threshold_low value '%s'\n", value);
        return false;
    } else if (strcmp(property_id, "charge-threshold-high") == 0) {
        int threshold = atoi(value);
        if (threshold > 0 && threshold < 100) {
            Serial.printf("BMS: Setting charge high-threshold to %d%%\n", threshold);
            _np_charge_threshold_high.set_value_and_publish(threshold);
            return true;
        }
        Serial.printf("BMS: Invalid charge_threshold_high value '%s'\n", value);
        return false;
    } else if (strcmp(property_id, "peak-begin-min") == 0) {
        int minutes = atoi(value);
        if (minutes > 0 && minutes < 1440) {
            Serial.printf("BMS: Setting peak start to %d min (%02d:%02d)\n",
                minutes, minutes / 60, minutes % 60);
            _np_peak_begin_min.set_value_and_publish(minutes);
            return true;
        }
        Serial.printf("BMS: Invalid peak_begin_min value '%s'\n", value);
        return false;
    } else if (strcmp(property_id, "peak-end-min") == 0) {
        int minutes = atoi(value);
        if (minutes > 0 && minutes < 1440) {
            Serial.printf("BMS: Setting peak end to %d min (%02d:%02d)\n",
                minutes, minutes / 60, minutes % 60);
            _np_peak_end_min.set_value_and_publish(minutes);
            return true;
        }
        Serial.printf("BMS: Invalid peak_end_min value '%s'\n", value);
        return false;
    } else if (strcmp(property_id, "threshold-discharge-daily") == 0) {
        bool enable = (strcmp(value, "true") == 0);
        Serial.printf("BMS: threshold_discharge_daily -> %s\n", enable ? "true" : "false");
        _np_threshold_discharge_daily.set_value_and_publish(enable ? "true" : "false");
        if (!enable) {
            _in_discharge_phase = false;  // cancel any active discharge wait
        }
        return true;
    }
    return false;
}

