#pragma once
// Host stand-in for the arduino-mqtt client: records every publish in order, so a test
// can assert the exact Homie lifecycle sequence the device put on the wire.
#include <stdio.h>
#include <string.h>

class MQTTClient {
 public:
    static const int MAX_RECORDS = 64;
    static const int TOPIC_MAX = 160;
    static const int PAYLOAD_MAX = 16;   // enough for any $state; descriptions are cut short

    struct Record {
        char topic[TOPIC_MAX];
        char payload[PAYLOAD_MAX];
    };

    bool publish(const char* topic, const char* payload, bool retained, int qos) {
        (void)retained;
        (void)qos;
        if (_count < MAX_RECORDS) {
            snprintf(_records[_count].topic, TOPIC_MAX, "%s", topic);
            snprintf(_records[_count].payload, PAYLOAD_MAX, "%s", payload);
        }
        _count++;
        return true;
    }
    bool subscribe(const char* topic, int qos) { (void)topic; (void)qos; return true; }

    void clear() { _count = 0; }
    int count() const { return _count; }
    const Record& at(int i) const { return _records[i]; }

 private:
    Record _records[MAX_RECORDS];
    int _count = 0;
};
