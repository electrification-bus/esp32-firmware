#pragma once
#include <node/NodeEntity.h>
#include <ble/ble_client.h>

// A generic BLE GATT device node — no protocol knowledge of any kind.
//
// Point it at a service plus a notify and (optionally) a write characteristic, and it:
//   - connects, reconnects, and caches the resolved address in NVS
//   - publishes every notification payload as hex
//   - sends hex bytes you publish to its `write` property
//
// That is enough to bring up an unfamiliar BLE device from an MQTT client before writing
// a line of protocol code: watch `last-notify` while poking `write`, and work out the
// framing interactively.
//
// It is also the reference for how a real protocol driver is built — examples/bms/ is
// exactly this plus JBD frame encoding/decoding.

#define GATT_HEX_MAX 128   // bytes of a notification we render to hex (2 chars each)

class GattDevice : public NodeEntity {
public:
    GattDevice(const char* homie_node_id, const char* address, const char* name_filter,
               const char* service_uuid, const char* notify_uuid, const char* write_uuid)
        : NodeEntity(homie_node_id), _cfg_address(address), _name_filter(name_filter),
          _service_uuid(service_uuid), _notify_uuid(notify_uuid), _write_uuid(write_uuid) {}
    ~GattDevice() {}

    bool setup() override;
    void loop() override;

    void entity_settable_callback(Property* property) override;
    void entity_settable_callback(const char* property_id, const char* value) override;

private:
    // Static trampoline — fires on the NimBLE task, so it only copies bytes and sets a
    // flag. Publishing happens in loop(), on the Arduino task.
    static void on_rx(void* ctx, const uint8_t* data, size_t len);
    void handleRx(const uint8_t* data, size_t len);
    bool sendHex(const char* hex);

    BLEClientLink _link;

    const char* _cfg_address;
    const char* _name_filter;
    const char* _service_uuid;
    const char* _notify_uuid;
    const char* _write_uuid;

    NodeProperty _np_connected;
    NodeProperty _np_address;
    NodeProperty _np_rssi;
    NodeProperty _np_last_notify;
    NodeProperty _np_notify_count;
    NodeProperty _np_write;
    NodeProperty _np_rescan;

    uint8_t  _rx[GATT_HEX_MAX];
    size_t   _rx_len       = 0;
    volatile bool _rx_ready = false;
    uint32_t _notify_count = 0;
    bool     _last_connected = false;
    uint32_t _last_status_ms = 0;
};
