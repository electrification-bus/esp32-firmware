#pragma once

#include <node/NodeEntity.h>

// JBD/Jiabaidu Smart BMS over BLE (GATT)
// Derived from BTBatApp: BmsProtocol.kt + BtManager.kt
//
// BLE UUIDs:
//   Service: 0000ff00-...   Notify: 0000ff01-...   Write: 0000ff02-...
//
// Frame format: 0xDD, R/W, reg, len, data..., checksum_hi, checksum_lo, 0x77

#define BMS_MAX_CELLS       16
#define BMS_MAX_TEMPS        4
#define BMS_FRAME_MAX      128
#define BMS_POLL_INTERVAL 30000
#define BMS_BLE_TIMEOUT   3000
#define BMS_SCAN_DURATION      5  // seconds
#define BMS_RECONNECT_INTERVAL 30000  // ms between reconnect attempts
#define BMS_MAX_PREF_FAILURES  3  // clear saved address after this many consecutive failures

// Decoded basic info (register 0x03)
struct BmsBasicInfo {
    float voltage;          // V
    float current;          // A (signed)
    float remain_cap;       // Ah
    float nominal_cap;      // Ah
    uint16_t cycles;
    uint8_t soc;            // %
    uint8_t mosfet_status;  // bit0=chg, bit1=dsg (1=OFF)
    uint8_t num_temps;
    float temps[BMS_MAX_TEMPS]; // degrees C
};

// Decoded cell voltages (register 0x04)
struct BmsCellVoltages {
    uint8_t num_cells;
    float cells[BMS_MAX_CELLS]; // V
};

class SerialBMS : public NodeEntity {
public:
    SerialBMS(const char* homie_node_id);
    ~SerialBMS() {};

    bool setup() override;
    void loop() override;
    bool pollIntervalTrigger() override;

    // BLE connection management
    bool scanAndConnect();  // scan for BMS, try preferred first, connect to first found
    bool connectToDevice(const char* ble_address);
    void disconnect();
    bool isConnected();

    // BMS queries
    bool queryBasicInfo();
    bool queryCellVoltages();

    // MOSFET control
    bool setMosfetAllOn();
    bool setMosfetChgOff();
    bool setMosfetDsgOff();
    bool setMosfetAllOff();

    // Accessors
    const BmsBasicInfo* getBasicInfo() const { return &_basic_info; }
    const BmsCellVoltages* getCellVoltages() const { return &_cell_voltages; }

    // Set BLE address to auto-connect on setup
    void setBleAddress(const char* address);

    // BLE notification handler (called from static callback)
    void onNotifyData(const uint8_t* data, size_t length);

private:
    // BLE internals
    bool _connected;
    char _ble_address[18];  // "XX:XX:XX:XX:XX:XX"

    // Response frame buffer
    uint8_t _rx_buf[BMS_FRAME_MAX];
    uint8_t _rx_len;
    bool _frame_ready;

    // Protocol
    bool sendCommand(const uint8_t* cmd, uint8_t cmd_len);
    bool waitForFrame(unsigned long timeout_ms);
    bool decodeBasicInfo(const uint8_t* buf, uint8_t len);
    bool decodeCellVoltages(const uint8_t* buf, uint8_t len);

    // NVS preferences for BLE address persistence
    void loadPreferredAddress();
    void savePreferredAddress(const char* address);
    void clearPreferredAddress();

    // Decoded data
    BmsBasicInfo _basic_info;
    BmsCellVoltages _cell_voltages;

    // Timing
    unsigned long _poll_timestamp;
    unsigned long _last_success;
    unsigned long _last_failure;
    int _poll_interval;

    // Reconnect state
    unsigned long _last_reconnect_attempt;
    uint8_t _consecutive_failures;  // counts failed connection attempts on preferred address
    bool _ble_initialized;

    // Homie properties
    NodeProperty _np_voltage;
    NodeProperty _np_current;
    NodeProperty _np_soc;
    NodeProperty _np_remain_cap;
    NodeProperty _np_nominal_cap;
    NodeProperty _np_cycles;
    NodeProperty _np_mosfet;
    NodeProperty _np_temperature;
    NodeProperty _np_cell_voltages;
};
