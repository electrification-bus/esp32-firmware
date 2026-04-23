#pragma once
#include <Arduino.h>
#include <node/NodeEntity.h>

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_UPDATING,
    OTA_STATE_REBOOTING,
    OTA_STATE_ERROR
} OTAState;

class OTANode : public NodeEntity {
public:
    OTANode(const char* homie_node_id);
    ~OTANode() {};

    bool setup() override;
    void loop() override;
    void entity_settable_callback(Property* property) override;

    // OTA operations
    void startUpdate(const char* url);
    void setEnabled(bool enabled);
    bool isEnabled() { return _enabled; }

private:
    // State management
    OTAState _state;
    bool _enabled;
    int _progress;
    char _version[32];
    char _firmware_url[256];
    char _checksum[65];  // SHA256 hex string
    char _message[128];

    // Properties
    NodeProperty _enabled_property;
    NodeProperty _state_property;
    NodeProperty _progress_property;
    NodeProperty _version_property;
    NodeProperty _firmware_url_property;
    NodeProperty _checksum_property;
    NodeProperty _message_property;

    // Internal methods
    void setState(OTAState state);
    void setProgress(int progress);
    void setMessage(const char* msg);
    const char* stateToString(OTAState state);
    bool downloadAndUpdate(const char* url);
    bool verifyChecksum(const char* expected);
};