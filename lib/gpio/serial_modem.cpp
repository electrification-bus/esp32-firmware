#include <Arduino.h>
#include <gpio/serial_modem.h>
#include <platform/mqtt_client.h>

#define MODEM_PROBE_INTERVAL_MS     200
#define MODEM_PROBE_MAX_ATTEMPTS    30
#define MODEM_NET_POLL_MS           2000
#define MODEM_SIGNAL_INTERVAL_MS    30000
#define MODEM_SMS_SCAN_INTERVAL_MS  1500
#define MODEM_READ_TIMEOUT_MS       2000
#define MODEM_SEND_TIMEOUT_MS       10000

SerialModem::SerialModem(const char* homie_node_id, Stream& modem_stream)
    : NodeEntity(homie_node_id), _modem(modem_stream) {
    _state           = MODEM_PROBE;
    _probe_attempts  = 0;
    _send_pending    = false;
    _last_state_ms   = 0;
    _last_signal_ms  = 0;
    _phone_num[0]    = '\0';
    _read_buf[0]     = '\0';
    _received_buf[0] = '\0';
    _pending_msg[0]  = '\0';
}

bool SerialModem::setup() {
    NodeEntity::setup();
    _phone_property   .setup(getNodeId(), "phone",    &NodeEntity::entity_settable_callback, this);
    _send_property    .setup(getNodeId(), "send",     &NodeEntity::entity_settable_callback, this);
    _received_property.setup(getNodeId(), "received");
    _status_property  .setup(getNodeId(), "status");
    _signal_property  .setup(getNodeId(), "signal");

    _publish_status("offline");
    Serial.printf("SerialModem: setup node '%s'\n", getNodeId());
    return true;
}

// ---- helpers ----------------------------------------------------------------

void SerialModem::_flush_modem() {
    unsigned long start = millis();
    while (millis() - start < 100) {
        while (_modem.available()) {
            _modem.read();
        }
    }
}

// Read into _read_buf until boundary string found or timeout.
// Returns number of chars read, -1 on timeout.
int SerialModem::_read_until(const char* boundary, int timeout_ms) {
    int cnt = 0;
    unsigned long start = millis();
    memset(_read_buf, 0, sizeof(_read_buf));
    do {
        while (_modem.available() && cnt < (int)sizeof(_read_buf) - 1) {
            _read_buf[cnt++] = (char)_modem.read();
            _read_buf[cnt]   = '\0';
            if (strstr(_read_buf, boundary)) {
                return cnt;
            }
        }
    } while (millis() - start < (unsigned long)timeout_ms);
    return -1;
}

// Send an AT command, wait for "OK". Returns true if OK received.
bool SerialModem::_at_cmd_ok(const char* cmd, int timeout_ms) {
    _modem.printf("%s\r\n", cmd);
    return _read_until("\r\nOK\r\n", timeout_ms) >= 0;
}

void SerialModem::_publish_status(const char* status) {
    Serial.printf("SerialModem: status -> %s\n", status);
    _status_property.set_value_and_publish(status);
}

// Publish received SMS - bypasses NodeProperty._value[32] limit
void SerialModem::_publish_received(const char* from, const char* msg) {
    snprintf(_received_buf, sizeof(_received_buf), "%s:%s", from, msg);
    Serial.printf("SerialModem: received from '%s': '%s'\n", from, msg);
    mqtt_queue_publish(_received_property.topic(), _received_buf, false);
}

// ---- SMS I/O ----------------------------------------------------------------

void SerialModem::_read_stored_message(int idx) {
    // AT+CMGR=<idx> returns:
    // +CMGR: "REC UNREAD","+14085069046",,"25/02/06,14:20:23-32"\r\n
    // <message text>\r\n\r\nOK\r\n
    _modem.printf("AT+CMGR=%d\r\n", idx);
    if (_read_until("\r\nOK\r\n", MODEM_READ_TIMEOUT_MS) < 0) {
        Serial.printf("SerialModem: CMGR timeout for idx %d\n", idx);
        return;
    }

    // Extract phone number
    char from[20] = {0};
    char* quote1 = strchr(_read_buf, '"');
    if (quote1) {
        char* quote2 = strchr(quote1 + 1, '"'); // end of "REC UNREAD"
        if (quote2) {
            char* pq1 = strchr(quote2 + 1, '"'); // skip comma, start of phone
            if (pq1) {
                char* pq2 = strchr(pq1 + 1, '"');
                if (pq2) {
                    int len = pq2 - pq1 - 1;
                    if (len > 0 && len < (int)sizeof(from) - 1) {
                        strncpy(from, pq1 + 1, len);
                        from[len] = '\0';
                    }
                }
            }
        }
    }

    // Extract message: after last "\r\n" before "\r\nOK"
    char* ok_ptr = strstr(_read_buf, "\r\nOK\r\n");
    if (ok_ptr && ok_ptr > _read_buf) {
        // Walk backwards to find preceding \r\n
        char* msg_end = ok_ptr;
        char* msg_start = msg_end - 1;
        while (msg_start > _read_buf && !(*msg_start == '\n' && *(msg_start - 1) == '\r')) {
            msg_start--;
        }
        if (*msg_start == '\n') msg_start++;
        char msg[170] = {0};
        int mlen = msg_end - msg_start;
        if (mlen > 0 && mlen < (int)sizeof(msg) - 1) {
            strncpy(msg, msg_start, mlen);
            msg[mlen] = '\0';
        }
        _publish_received(from[0] ? from : "unknown", msg);
    }

    // Delete the stored message to free SIM storage
    _modem.printf("AT+CMGD=%d\r\n", idx);
    _read_until("\r\nOK\r\n", MODEM_READ_TIMEOUT_MS);
}

void SerialModem::_scan_for_sms() {
    // Non-blocking: check if modem has sent a +CMTI notification
    if (!_modem.available()) return;

    int cnt = 0;
    unsigned long start = millis();
    memset(_read_buf, 0, sizeof(_read_buf));
    while (_modem.available() && cnt < (int)sizeof(_read_buf) - 1 && millis() - start < 200) {
        _read_buf[cnt++] = (char)_modem.read();
        _read_buf[cnt]   = '\0';
    }
    if (cnt == 0) return;

    Serial.printf("SerialModem: modem data: %s\n", _read_buf);

    // +CMTI: "SM",<idx> or +CMTI: "ME",<idx>
    char* cmti = strstr(_read_buf, "+CMTI:");
    if (cmti) {
        char* comma = strchr(cmti, ',');
        if (comma) {
            int msg_idx = atoi(comma + 1);
            Serial.printf("SerialModem: incoming SMS notification idx=%d\n", msg_idx);
            _read_stored_message(msg_idx);
        }
    }

    // +SMS FULL - delete all to make room
    if (strstr(_read_buf, "+SMS FULL")) {
        Serial.printf("SerialModem: SMS storage full - deleting all\n");
        for (int i = 0; i < 20; i++) {
            _modem.printf("AT+CMGD=%d\r\n", i);
            _read_until("\r\nOK\r\n", 500);
        }
    }
}

void SerialModem::_send_sms(const char* phone, const char* msg) {
    Serial.printf("SerialModem: sending SMS to '%s': '%s'\n", phone, msg);
    _modem.printf("AT+CMGS=\"%s\"\r\n", phone);
    // Wait for '>' prompt
    if (_read_until(">", 5000) < 0) {
        Serial.printf("SerialModem: SMS prompt timeout\n");
        return;
    }
    // Send message body terminated with Ctrl-Z (0x1A)
    _modem.print(msg);
    _modem.write(0x1A);
    // Wait for OK or +CMGS: response
    if (_read_until("\r\nOK\r\n", MODEM_SEND_TIMEOUT_MS) >= 0) {
        Serial.printf("SerialModem: SMS sent OK\n");
    } else {
        Serial.printf("SerialModem: SMS send failed\n");
    }
}

// ---- state machine ----------------------------------------------------------

void SerialModem::_run_state_machine() {
    unsigned long now = millis();

    switch (_state) {
        case MODEM_PROBE:
            if (now - _last_state_ms < MODEM_PROBE_INTERVAL_MS) return;
            _last_state_ms = now;
            _modem.print("AT\r\n");
            if (_read_until("\r\nOK\r\n", 200) >= 0) {
                Serial.printf("SerialModem: modem responded to AT\n");
                _probe_attempts = 0;
                _state = MODEM_CONFIGURE;
            } else {
                if (++_probe_attempts % 10 == 0) {
                    Serial.printf("SerialModem: no AT response (attempt %d)\n", _probe_attempts);
                }
            }
            break;

        case MODEM_CONFIGURE:
            Serial.printf("SerialModem: configuring modem\n");
            _flush_modem();
            // Set SMS text mode
            if (!_at_cmd_ok("AT+CMGF=1", 2000)) {
                Serial.printf("SerialModem: CMGF=1 failed, retrying\n");
                _state = MODEM_PROBE;
                break;
            }
            // New message notification: store and notify with index
            if (!_at_cmd_ok("AT+CNMI=2,1,0,0,0", 2000)) {
                Serial.printf("SerialModem: CNMI failed, retrying\n");
                _state = MODEM_PROBE;
                break;
            }
            Serial.printf("SerialModem: configured, waiting for network\n");
            _publish_status("registering");
            _last_state_ms = now;
            _state = MODEM_WAIT_NET;
            break;

        case MODEM_WAIT_NET:
            if (now - _last_state_ms < MODEM_NET_POLL_MS) return;
            _last_state_ms = now;
            _modem.print("AT+CREG?\r\n");
            if (_read_until("\r\nOK\r\n", 2000) >= 0) {
                // +CREG: 0,1 = registered home, +CREG: 0,5 = roaming
                char* creg = strstr(_read_buf, "+CREG:");
                if (creg) {
                    char* comma = strchr(creg, ',');
                    if (comma) {
                        int reg_status = atoi(comma + 1);
                        if (reg_status == 1 || reg_status == 5) {
                            Serial.printf("SerialModem: network registered (status=%d)\n", reg_status);
                            _publish_status("online");
                            _last_signal_ms = 0; // force signal read immediately
                            _last_state_ms = now;
                            _state = MODEM_ONLINE;
                        } else if (_probe_attempts++ % 10 == 0) {
                            Serial.printf("SerialModem: waiting for network, CREG status=%d\n", reg_status);
                        }
                    }
                }
            } else {
                Serial.printf("SerialModem: CREG timeout - back to probe\n");
                _state = MODEM_PROBE;
                _publish_status("offline");
            }
            break;

        case MODEM_ONLINE:
            // Periodic signal quality report
            if (now - _last_signal_ms >= (unsigned long)MODEM_SIGNAL_INTERVAL_MS) {
                _last_signal_ms = now;
                _modem.print("AT+CSQ\r\n");
                if (_read_until("\r\nOK\r\n", 1000) >= 0) {
                    char* csq = strstr(_read_buf, "+CSQ:");
                    if (csq) {
                        int rssi = atoi(csq + 5);
                        _signal_property.set_value_and_publish(rssi);
                        if (rssi == 99) {
                            // No signal - re-register
                            Serial.printf("SerialModem: signal lost, re-registering\n");
                            _publish_status("registering");
                            _state = MODEM_WAIT_NET;
                            _probe_attempts = 0;
                        }
                    }
                } else {
                    Serial.printf("SerialModem: CSQ timeout - back to probe\n");
                    _state = MODEM_PROBE;
                    _publish_status("offline");
                    break;
                }
            }

            // Send pending SMS if any
            if (_send_pending && _phone_num[0] != '\0') {
                _send_pending = false;
                _send_sms(_phone_num, _pending_msg);
                _pending_msg[0] = '\0';
            }

            // Scan for incoming SMS
            _scan_for_sms();
            break;
    }
}

// ---- NodeEntity interface ---------------------------------------------------

void SerialModem::loop() {
    _run_state_machine();
}

void SerialModem::entity_settable_callback(const char* property_id, const char* value) {
    if (strcmp(property_id, "phone") == 0) {
        strncpy(_phone_num, value, sizeof(_phone_num) - 1);
        _phone_num[sizeof(_phone_num) - 1] = '\0';
        Serial.printf("SerialModem: phone set to '%s'\n", _phone_num);
        _phone_property.set_value_and_publish(_phone_num);

    } else if (strcmp(property_id, "send") == 0) {
        if (_phone_num[0] == '\0') {
            Serial.printf("SerialModem: send ignored - no phone number set\n");
            return;
        }
        strncpy(_pending_msg, value, sizeof(_pending_msg) - 1);
        _pending_msg[sizeof(_pending_msg) - 1] = '\0';
        if (_state == MODEM_ONLINE) {
            _send_pending = true;
        } else {
            Serial.printf("SerialModem: send queued - modem not online yet\n");
            _send_pending = true;
        }

    } else {
        Serial.printf("SerialModem: unknown property '%s'\n", property_id);
    }
}
