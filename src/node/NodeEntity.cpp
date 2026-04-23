#include <Arduino.h>
#include <etc/config.h>
#include <util/jsonUtils.h>
#include <node/NodeEntity.h>

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
    strncpy(_homie_node_id, id, sizeof(_homie_node_id) - 1);
    _homie_node_id[sizeof(_homie_node_id) - 1] = '\0'; // Ensure null termination
}
bool NodeEntity::setup() {
    Serial.printf("NodeEntity::setup() '%s'\n", _homie_node_id);
    return true;
}

bool NodeEntity::setup(const char* homie_node_id) {
    setHomieNodeId(homie_node_id);
    return setup();
}

//returns local alloc - caller must not retain
const char* NodeEntity::getPropertyTopic(const char* property_id, char* buf) {
  sprintf(buf, "%s%s/%s", theDevice.topic(), _homie_node_id, property_id);
  return buf;
}

void NodeEntity::entity_settable_callback(Property* property) {
    Serial.printf("NodeEntity: entity_settable_callback(Property*) not implemented for node id: %s\n", _homie_node_id);
}

void NodeEntity::entity_settable_callback(const char* property_id, const char* value) {
    Serial.printf("NodeEntity: entity_settable_callback('%s','%s') not implemented for node id: %s\n",
        property_id, value, _homie_node_id);
}