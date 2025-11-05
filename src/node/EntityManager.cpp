#include <Arduino.h>
#include <node/NodeEntity.h>
#include <node/EntityManager.h>
#include <config.h>
#include <util/jsonUtils.h>

EntityManager*  entityManager;

void EntityManager::addDevice(Device* dev) {
    _device = dev;
};

Node* EntityManager::getNode(const char* id) {
    return _device->getNode(id);
};

void EntityManager::publish(const char* node_id) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("EntityManager: Node '%s' not found for publishing\n", node_id);
        return;
    }
    node->publish();
};

Property* EntityManager::validate(const char* node_id, const char* property_id) {
    Node* node = getNode(node_id);
    if (node == nullptr) {
        Serial.printf("EntityManager: Node '%s' not found for property '%s'\n", node_id, property_id);
        return nullptr;
    }
    Property* property = node->getProperty(property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s'\n", property_id, node_id);
        return nullptr;
    }
    return property;
}


//TODO handle all property types
void EntityManager::setNodeProperty(const char* node_id, const char* property_id, int value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("EntityManager: Set property '%s' of Node '%s' to value %s\n", property_id, node_id, value ? "true":"false");
};
void EntityManager::setNodeProperty(const char* node_id, const char* property_id, const char* value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("EntityManager: Set property '%s' of Node '%s' to value %s\n", property_id, node_id, value ? "true":"false");
};
void EntityManager::setNodeProperty(const char* node_id, const char* property_id, bool value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("EntityManager: Set property '%s' of Node '%s' to value %s\n", property_id, node_id, value ? "true":"false");
};
void EntityManager::setNodeProperty(const char* node_id, const char* property_id, unsigned int value) {
        Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("EntityManager: Set property '%s' of Node '%s' to value %s\n", property_id, node_id, value ? "true":"false");
};
void EntityManager::setNodeProperty(const char* node_id, const char* property_id, float value) {
    Property* property = validate(node_id, property_id);
    if (property == nullptr) {
        Serial.printf("EntityManager: Property '%s' not found in Node '%s' for setting value %.2f\n", property_id, node_id, value);
        return;
    }
    property->setValue(value);
    Serial.printf("EntityManager: Set property '%s' of Node '%s' to value %.2f\n", property_id, node_id, value);
};  