#include <Arduino.h>
#include <config.h>
#include <util/jsonUtils.h>
#include <node/Entity.h>

Entity::Entity(){};
Entity::~Entity(){};

const char* Entity::getHomieNodeId() {
    return homie_node_id;
}

bool Entity::pollIntervalTrigger() {
    return false;
}

const char* Entity::getClassName() const {
    return "Entity";
}

void Entity::setHomieNodeId(const char* id) {
    strncpy(homie_node_id, id, sizeof(homie_node_id) - 1);
    homie_node_id[sizeof(homie_node_id) - 1] = '\0'; // Ensure null termination
}

bool Entity::setup(const char* node_id) {
    setHomieNodeId(node_id);
    return setup();
}

bool Entity::setup() {
    JsonDocument* node_conf = get_node_config();
    //TODO remove this hardcoding and map the YAML 'voltage_current_detector' node to the Modbus_Eletechsup32 (consolidate with BME280 setup())
    if (jsonExists((*node_conf)[HOMIE_NODES].as<JsonVariant>())) {
        JsonVariant nodes = (*node_conf)[HOMIE_NODES].as<JsonVariant>();
        for (JsonVariant node : nodes.as<JsonArray>()) {
            if (strcmp(node[HOMIE_ID].as<const char*>(), homie_node_id) == 0) { //read this from node config? (map environment to BME280)
                Serial.printf("SETUP: ModbusEntity: found node: %s\n", homie_node_id);
                //pull out properties and map to Modbus_Eletechsup32 device
                JsonVariant props = node[HOMIE_PROPERTIES].as<JsonVariant>();
                for (JsonVariant props : props.as<JsonArray>()) {
                    //nodeManager->mapProperty(props[HOMIE_ID].as<const char*>(), props[HOMIE_NAME].as<const char*>(), props[HOMIE_TYPE].as<const char*>());
                }
                Serial.printf("%s: mapped to homie node id: %s\n", getNodeName(), homie_node_id);
                setHomieNodeId(node[HOMIE_ID].as<const char*>());
                break;
            }
        }
    } else {
        Serial.printf("SETUP: Entity: ERROR: node not found in config:'%s' - skipping setup\n", homie_node_id); //TODO
        return false;
    }
    return nodeBegin();
} 
