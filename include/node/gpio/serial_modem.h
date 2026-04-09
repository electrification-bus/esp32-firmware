#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>

enum ModemState {
    MODEM_PROBE,
    MODEM_CONFIGURE,
    MODEM_WAIT_NET,
    MODEM_ONLINE,
};

class SerialModem : public NodeEntity {
public:
    SerialModem(const char* homie_node_id, Stream& modem_stream);
    bool setup() override;
    void loop() override;
    void entity_settable_callback(const char* property_id, const char* value) override;

    ModemState getState() const { return _state; }

private:
    Stream&      _modem;
    ModemState   _state;

    NodeProperty _phone_property;     // settable: destination phone number
    NodeProperty _send_property;      // settable: message to send (triggers SMS)
    NodeProperty _received_property;  // published: last received SMS ("from:message")
    NodeProperty _status_property;    // published: modem status string
    NodeProperty _signal_property;    // published: signal quality (0-31)

    char _phone_num[20];
    char _read_buf[512];
    char _received_buf[200];   // larger than NodeProperty._value[32] - published directly

    unsigned long _last_state_ms;
    unsigned long _last_signal_ms;
    int  _probe_attempts;
    bool _send_pending;
    char _pending_msg[170];    // SMS to send on next ONLINE tick

    void _run_state_machine();
    bool _at_cmd_ok(const char* cmd, int timeout_ms);
    int  _read_until(const char* boundary, int timeout_ms);
    void _flush_modem();
    void _scan_for_sms();
    void _read_stored_message(int idx);
    void _send_sms(const char* phone, const char* msg);
    void _publish_status(const char* status);
    void _publish_received(const char* from, const char* msg);
};
