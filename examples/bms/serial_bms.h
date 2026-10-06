#pragma once

#include <node/NodeEntity.h>
#include <ble/ble_client.h>
#include <gpio/digital_output.h>

// JBD / Xiaoxiang Smart BMS over BLE (GATT).
// Protocol derived from BTBatApp: BmsProtocol.kt + BtManager.kt (reference only).
//
// ─────────────────────────────────────────────────────────────────────────────
// THIS IS A COPY/PASTE EXAMPLE, NOT A BUILDABLE COMPONENT.
//
// `ebus-esp32 generate` only discovers components under lib/<bus>/<name>.yml, so
// device.yml cannot reference this file where it sits. To use it:
//
//     cp examples/bms/serial_bms.{h,cpp,yml} lib/ble/
//
// then add `- component: ble/serial_bms` to device.yml. See README.md in this folder.
// ─────────────────────────────────────────────────────────────────────────────
//
// It is kept as an example rather than a shipped component because most of what makes it
// interesting is device-specific: the JBD frame format, the register map, and a charging
// policy that suits one particular battery installation. What is genuinely reusable —
// connect, reconnect, subscribe, cache the address — lives in lib/ble/ble_client.* and is
// shared with lib/ble/gatt_device.
//
// Frame format: 0xDD, R/W, reg, len, data..., checksum_hi, checksum_lo, 0x77
//
//   Service 0000ff00-…   Notify 0000ff01-…   Write 0000ff02-…

#define BMS_MAX_CELLS       16
#define BMS_MAX_TEMPS        4
#define BMS_FRAME_MAX      128
#define BMS_POLL_INTERVAL 30000
#define BMS_BLE_TIMEOUT   3000

// Decoded basic info (register 0x03)
struct BmsBasicInfo {
    float voltage;          // V
    float current;          // A (signed)
    float remain_cap;       // Ah
    float nominal_cap;      // Ah
    uint16_t cycles;
    uint8_t soc;            // %
    uint8_t mosfet_status;  // bit0=CHG, bit1=DSG (status: 1=enabled; control cmd: 1=disable)
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

    // BMS queries
    bool queryBasicInfo();
    bool queryCellVoltages();

    // MOSFET control
    bool setMosfetAllOn();
    bool setMosfetChgOff();
    bool setMosfetDsgOff();
    bool setMosfetAllOff();

    // Charge control policy
    void charge_manager();

    // Accessors
    const BmsBasicInfo* getBasicInfo() const { return &_basic_info; }
    const BmsCellVoltages* getCellVoltages() const { return &_cell_voltages; }

    // Settable callbacks for MQTT
    bool entity_settable_callback(Property* property) override;
    bool entity_settable_callback(const char* property_id, const char* value) override;
    void set(bool onOff) override;
    bool setChargeControl(bool onOff);   // false when the MOSFET command fails

    // Mirror charge-control onto an AC contactor. NOTE: the generator cannot wire a
    // pointer between two nodes, so this only works from a hand-coded src/homie.cpp
    // (doc/HandCodedDevice.md). deliver_local_set() is the cleaner route — address the
    // relay as <node>/<property> and drop the pointer entirely. See README.md.
    void setContactor(DigitalOutput* out) { _ssr = out; }

private:
    // Transport — everything BLE lives here, shared with lib/ble/gatt_device.
    BLEClientLink _link;
    static void on_ble_rx(void* ctx, const uint8_t* data, size_t len);
    void handleRx(const uint8_t* data, size_t len);

    // Protocol
    bool sendCommand(const uint8_t* cmd, uint8_t cmd_len);
    bool waitForFrame(unsigned long timeout_ms);
    bool decodeBasicInfo(const uint8_t* buf, uint8_t len);
    bool decodeCellVoltages(const uint8_t* buf, uint8_t len);

    // Policy
    bool isPeakTime();

    DigitalOutput* _ssr;  // optional AC contactor mirrored from charge-control

    // Peak/off-peak scheduling state
    bool _peak_state_known;
    bool _last_peak_state;
    bool _in_discharge_phase;

    // Response frame buffer, filled from the BLE notify callback
    uint8_t _rx_buf[BMS_FRAME_MAX];
    volatile uint8_t _rx_len;
    volatile bool _frame_ready;

    // Decoded data
    BmsBasicInfo _basic_info;
    BmsCellVoltages _cell_voltages;

    // Timing
    unsigned long _poll_timestamp;
    unsigned long _last_success;
    unsigned long _last_failure;
    int _poll_interval;

    // Homie properties
    NodeProperty _np_voltage;
    NodeProperty _np_current;
    NodeProperty _np_soc;
    NodeProperty _np_remain_cap;
    NodeProperty _np_nominal_cap;
    NodeProperty _np_cycles;
    NodeProperty _np_mosfet_charge;    // settable: controls CHG MOSFET
    NodeProperty _np_mosfet_discharge; // read-only: DSG MOSFET state
    NodeProperty _np_temperature;
    NodeProperty _np_cell_voltages;
    NodeProperty _np_charge_control;   // settable: on/off, mirrored to the SSR
    NodeProperty _np_rescan;           // settable: clear the cached BLE address, rescan
    NodeProperty _np_charge_threshold_low;
    NodeProperty _np_charge_threshold_high;
    NodeProperty _np_peak_begin_min;
    NodeProperty _np_peak_end_min;
    NodeProperty _np_threshold_discharge_daily;
};
