#include <node/ota_node.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Firmware version - update with each release
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

OTANode::OTANode(const char* homie_node_id) : NodeEntity(homie_node_id) {
    _state = OTA_STATE_IDLE;
    _enabled = true;
    _progress = 0;
    strcpy(_version, FIRMWARE_VERSION);
    memset(_firmware_url, 0, sizeof(_firmware_url));
    memset(_checksum, 0, sizeof(_checksum));
    memset(_message, 0, sizeof(_message));
}

bool OTANode::setup() {
    NodeEntity::setup();  // Base class first

    // Setup properties
    _enabled_property.setup_settable(getNodeId(), "enabled",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _state_property.setup(getNodeId(), "state");
    _progress_property.setup(getNodeId(), "progress");
    _version_property.setup(getNodeId(), "version");
    _firmware_url_property.setup_settable(getNodeId(), "firmware-url",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _checksum_property.setup_settable(getNodeId(), "checksum",
        &NodeEntity::entity_settable_callback, (NodeEntity*)this);
    _message_property.setup(getNodeId(), "message");

    // Publish initial values
    _enabled_property.set_value_and_publish(_enabled);
    _state_property.set_value_and_publish(stateToString(_state));
    _progress_property.set_value_and_publish(_progress);
    _version_property.set_value_and_publish(_version);
    _message_property.set_value_and_publish("Ready");
    return true;
}

const char* OTANode::stateToString(OTAState state) {
    switch (state) {
        case OTA_STATE_IDLE:        return "idle";
        case OTA_STATE_DOWNLOADING: return "downloading";
        case OTA_STATE_VERIFYING:   return "verifying";
        case OTA_STATE_UPDATING:    return "updating";
        case OTA_STATE_REBOOTING:   return "rebooting";
        case OTA_STATE_ERROR:       return "error";
        default:                    return "unknown";
    }
}

void OTANode::setState(OTAState state) {
    _state = state;
    _state_property.set_value_and_publish(stateToString(state));
}

void OTANode::setProgress(int progress) {
    _progress = progress;
    _progress_property.set_value_and_publish(progress);
}

void OTANode::setMessage(const char* msg) {
    strncpy(_message, msg, sizeof(_message) - 1);
    _message_property.set_value_and_publish(_message);
}

void OTANode::setEnabled(bool enabled) {
    _enabled = enabled;
    _enabled_property.set_value_and_publish(enabled);
}

void OTANode::entity_settable_callback(Property* property) {
    Serial.printf("OTA: settable callback for '%s' = '%s'\n",
                  property->id(), property->value());

    if (strcmp(property->id(), "enabled") == 0) {
        bool val = (strcmp(property->value(), "true") == 0);
        setEnabled(val);
        Serial.printf("OTA: enabled set to %s\n", val ? "true" : "false");
    }
    else if (strcmp(property->id(), "firmware-url") == 0) {
        if (!_enabled) {
            setMessage("OTA is disabled");
            setState(OTA_STATE_ERROR);
            return;
        }
        strncpy(_firmware_url, property->value(), sizeof(_firmware_url) - 1);
        Serial.printf("OTA: firmware URL set to '%s'\n", _firmware_url);
        startUpdate(_firmware_url);
    }
    else if (strcmp(property->id(), "checksum") == 0) {
        strncpy(_checksum, property->value(), sizeof(_checksum) - 1);
        Serial.printf("OTA: checksum set to '%s'\n", _checksum);
    }
}

void OTANode::startUpdate(const char* url) {
    if (_state != OTA_STATE_IDLE && _state != OTA_STATE_ERROR) {
        setMessage("Update already in progress");
        return;
    }

    if (strlen(url) == 0) {
        setMessage("No firmware URL provided");
        setState(OTA_STATE_ERROR);
        return;
    }

    Serial.printf("OTA: Starting update from %s\n", url);
    setMessage("Starting download...");

    if (downloadAndUpdate(url)) {
        setState(OTA_STATE_REBOOTING);
        setMessage("Update complete, rebooting...");
        delay(1000);
        ESP.restart();
    }
}

bool OTANode::downloadAndUpdate(const char* url) {
    HTTPClient http;
    WiFiClient client;

    setState(OTA_STATE_DOWNLOADING);
    setProgress(0);

    Serial.printf("OTA: Connecting to %s\n", url);

    if (!http.begin(client, url)) {
        setMessage("Failed to connect to server");
        setState(OTA_STATE_ERROR);
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "HTTP error: %d", httpCode);
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        setMessage("Invalid content length");
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    Serial.printf("OTA: Firmware size: %d bytes\n", contentLength);

    if (!Update.begin(contentLength)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Not enough space: %s", Update.errorString());
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    setState(OTA_STATE_UPDATING);
    setMessage("Writing firmware...");

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int written = 0;
    int lastProgress = 0;
    int totalSize = contentLength;

    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        size_t available = stream->available();
        if (available) {
            size_t toRead = (available < sizeof(buffer)) ? available : sizeof(buffer);
            size_t readBytes = stream->readBytes(buffer, toRead);

            size_t writtenBytes = Update.write(buffer, readBytes);
            if (writtenBytes != readBytes) {
                setMessage("Write error");
                setState(OTA_STATE_ERROR);
                Update.abort();
                http.end();
                return false;
            }

            written += writtenBytes;
            if (totalSize > 0) {
                contentLength -= writtenBytes;
            }

            // Update progress every 5%
            int progress = (written * 100) / totalSize;
            if (progress >= lastProgress + 5) {
                setProgress(progress);
                lastProgress = progress;
                Serial.printf("OTA: Progress %d%%\n", progress);
            }
        }
        delay(1);
    }

    setProgress(100);

    if (!Update.end(true)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Update failed: %s", Update.errorString());
        setMessage(msg);
        setState(OTA_STATE_ERROR);
        http.end();
        return false;
    }

    Serial.println("OTA: Update complete!");
    http.end();
    return true;
}

void OTANode::loop() {
    // OTA loop processing
    // Could add timeout handling, retry logic, etc.
}
