#pragma once
#include <ArduinoJson.h>
#include <homie/Device.h>
#include <homie/Node.h>
#include <node/NodeEntity.h>

class EntityManager {
    public:
        EntityManager(Device* device) : _device(device) {};
        EntityManager(){};
        ~EntityManager(){};

        void loop();

        void addDevice(Device* device);
        void addNodeEntity(const char* homie_node_id, NodeEntity* entity);
        Node* getNode(const char* id);
        NodeEntity* getEntity(const char* node_id);
        void publish(const char* node_id);
        Property* validate(const char* node_id, const char* property_id);
        void setNodeProperty(const char* node_id, const char* property_id, int value);
        void setNodeProperty(const char* node_id, const char* property_id, float value);
        void setNodeProperty(const char* node_id, const char* property_id, const char* value);
        void setNodeProperty(const char* node_id, const char* property_id, bool value);
        void setNodeProperty(const char* node_id, const char* property_id, unsigned int value);

        void setNodePropertyOnEntity(const char* node_id, const char* property_id);

        Node* operator[](const char* node_id_key) { return getNode(node_id_key); };

    private:
        NodeEntity* _entities[32];
        int _num_entities = 0;
        Device* _device;
        int _num_nodes = 0;
        int _num_properties_mapped = 0;
};

extern EntityManager* entityManager;

