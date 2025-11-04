#include <Arduino.h>
#include <config.h>
#include <util/jsonUtils.h>
#include <node/NodeEntity.h>

NodeEntity::NodeEntity(){};
NodeEntity::~NodeEntity(){};

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

bool NodeEntity::setup(const char* node_id) {
    setHomieNodeId(node_id);
    return setup();
}

bool NodeEntity::setup() {
    JsonDocument* node_conf = get_node_config();
    //TODO remove this hardcoding and map the YAML 'voltage_current_detector' node to the Modbus_Eletechsup32 (consolidate with BME280 setup())
    if (jsonExists((*node_conf)[HOMIE_NODES].as<JsonVariant>())) {
        JsonVariant nodes = (*node_conf)[HOMIE_NODES].as<JsonVariant>();
        for (JsonVariant node : nodes.as<JsonArray>()) {
            if (strcmp(node[HOMIE_ID].as<const char*>(), _homie_node_id) == 0) { //read this from node config? (map environment to BME280)
                Serial.printf("SETUP: NodeEntity: found node: %s\n", _homie_node_id);
                //pull out properties and map to Modbus_Eletechsup32 device
                JsonVariant props = node[HOMIE_PROPERTIES].as<JsonVariant>();
                for (JsonVariant props : props.as<JsonArray>()) {
                    //nodeManager->mapProperty(props[HOMIE_ID].as<const char*>(), props[HOMIE_NAME].as<const char*>(), props[HOMIE_TYPE].as<const char*>());
                }
                Serial.printf("%s: mapped to homie node id: %s\n", getNodeName(), _homie_node_id);
                setHomieNodeId(node[HOMIE_ID].as<const char*>());
                break;
            }
        }
    } else {
        Serial.printf("SETUP: NodeEntity: ERROR: node not found in config:'%s' - skipping setup\n", _homie_node_id); //TODO
        return false;
    }
    return nodeBegin();
} 

bool NodeEntity::nodeBegin() {
    Serial.printf("NodeEntity: nodeBegin() not implemented for node id: %s\n", _homie_node_id);
    return true;
}