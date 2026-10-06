#include <Arduino.h>
#include <platform/config.h>
#include <util/jsonUtils.h>
#include <node/NodeEntity.h>
#include <homie/homie_id.h>

NodeEntity::NodeEntity(const char* homie_node_id) {
    setHomieNodeId(homie_node_id);
}

const char* NodeEntity::getHomieNodeId() {
    return _homie_node_id;
}

bool NodeEntity::pollIntervalTrigger() {
    return false;
}

void NodeEntity::setHomieNodeId(const char* id) {
    // Coerce to a Homie-legal id. This is the single chokepoint: getNodeId()
    // feeds both Device::addNode() and the per-property NodeProperty::setup() calls,
    // so the published node topic and every property topic under it stay consistent.
    char sanitized[sizeof(_homie_node_id)] = {0};
    sanitize_homie_id(id, sanitized, sizeof(sanitized));
    if (sanitized[0] == '\0') {
        // All-illegal id would leave the node unaddressable; keep the raw value
        // (still null-bounded) and warn rather than silently drop the node.
        Serial.printf("NodeEntity: WARNING node id '%s' sanitizes to empty — keeping raw (NOT Homie-legal)\n", id);
        strncpy(_homie_node_id, id, sizeof(_homie_node_id) - 1);
        _homie_node_id[sizeof(_homie_node_id) - 1] = '\0';
        return;
    }
    if (strcmp(sanitized, id) != 0) {
        Serial.printf("NodeEntity: node id '%s' -> '%s' (Homie-legal: a-z 0-9 -)\n", id, sanitized);
    }
    strncpy(_homie_node_id, sanitized, sizeof(_homie_node_id) - 1);
    _homie_node_id[sizeof(_homie_node_id) - 1] = '\0'; // Ensure null termination
}
bool NodeEntity::setup() {
    // Record this entity's device as the active setup context so the
    // NodeProperty.setup() calls that follow (in the derived driver's setup())
    // build topics + load initial values against THIS device (root or child).
    g_active_setup_device = device();
    Serial.printf("NodeEntity::setup() '%s' (device '%s')\n", _homie_node_id, device()->getId());
    return true;
}

bool NodeEntity::setup(const char* homie_node_id) {
    setHomieNodeId(homie_node_id);
    return setup();
}

//returns local alloc - caller must not retain
const char* NodeEntity::getPropertyTopic(const char* property_id, char* buf, size_t bufSize) {
  snprintf(buf, bufSize, "%s%s/%s", device()->topic(), _homie_node_id, property_id);
  return buf;
}

bool NodeEntity::entity_settable_callback(Property* property) {
    Serial.printf("NodeEntity: entity_settable_callback(Property*) not implemented for node id: %s\n", _homie_node_id);
    return true;   // no driver behind it: accepted, as for a property with no callback
}

bool NodeEntity::entity_settable_callback(const char* property_id, const char* value) {
    Serial.printf("NodeEntity: entity_settable_callback('%s','%s') not implemented for node id: %s\n",
        property_id, value, _homie_node_id);
    return true;
}