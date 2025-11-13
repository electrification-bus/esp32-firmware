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

void EntityManager::addNodeEntity(const char* homie_node_id, NodeEntity* entity) {
    entity->setHomieNodeId(homie_node_id);
    _entities[_num_entities++] = entity;
    Serial.printf("EntityManager: Adding NodeEntity for Homie Node ID '%s'\n", homie_node_id);
};
NodeEntity* EntityManager::getEntity(const char* node_id) {
    for(int i=0;i<_num_entities;i++) {
        NodeEntity* entity = _entities[i];
        if (strcmp(entity->getHomieNodeId(), node_id) == 0) {
            return entity;
        }
    }
    return nullptr;
}
void EntityManager::setNodePropertyOnEntity(const char* node_id, const char* property_id) {
    Serial.printf("EntityManager: Homie Node '%s' property '%s' set\n", node_id, property_id);
        NodeEntity* entity = getEntity(node_id);
        if (entity == nullptr) {
            Serial.printf("EntityManager: Homie Node '%s' not found on any Entity\n", node_id);
            return;
        }
        if (strcmp(entity->getHomieNodeId(), node_id) == 0) {
            Node* d = theDevice->getNode(node_id);
            Property* p = d->getProperty(property_id);
            if (p == nullptr) {Serial.printf("EntityManager: property '%s' not found on Node '%s'\n", property_id, node_id); return;} 
            if (strcmp(p->datatype(), "float") == 0) {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - float value %.2f\n", node_id, entity->getNodeName(), p->getFloatValue());
                entity->set(p->getFloatValue());
            } else if (strcmp(p->datatype(), "integer") == 0) {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - int value %d\n", node_id, entity->getNodeName(), atoi(p->value()));
                entity->set(p->getIntValue());
            } else if (strcmp(p->datatype(), "boolean") == 0) {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - bool value %s\n", node_id, entity->getNodeName(), strcmp(p->value(),"true")==0 ? "true":"false");
                entity->set(p->getBoolValue());
            } else if (strcmp(p->datatype(), "enum") == 0) {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - enum value %s\n", node_id, entity->getNodeName(), strcmp(p->value(),"true")==0 ? "true":"false");
                entity->set("NOT IMPLEMENTED");
            } else if (strcmp(p->datatype(), "color") == 0) {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - color value %s\n", node_id, entity->getNodeName(), strcmp(p->value(),"true")==0 ? "true":"false");
                entity->set("NOT IMPLEMENTED");
            } else {
                Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s' - string value '%s'\n", node_id, entity->getNodeName(), p->value());
                entity->set(p->value());
            }
            Serial.printf("EntityManager: Homie Node '%s' found on Entity '%s'\n", node_id, entity->getNodeName());
        } else {
            Serial.printf("EntityManager: Homie Node '%s' not found on Entity '%s'\n", node_id, entity->getNodeName());
        }
    
}   

void EntityManager::loop() {
    for(int i=0;i<_num_entities;i++) {
        NodeEntity* entity = _entities[i];
        entity->loop();
    }
}   