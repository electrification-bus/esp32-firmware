#pragma once
#include <ArduinoJson.h>
#include <homie/Device.h>
#include <homie/Node.h>

class EntityManager {
    public:
        EntityManager(Device* device) : _device(device) {};
        EntityManager(){};
        ~EntityManager(){};

        void addDevice(Device* device);
        Node* getNode(const char* id);
        void publish(const char* node_id);
        Property* validate(const char* node_id, const char* property_id);
        void setNodeProperty(const char* node_id, const char* property_id, int value);
        void setNodeProperty(const char* node_id, const char* property_id, float value);
        void setNodeProperty(const char* node_id, const char* property_id, const char* value);
        void setNodeProperty(const char* node_id, const char* property_id, bool value);
        void setNodeProperty(const char* node_id, const char* property_id, unsigned int value);

        Node* operator[](const char* node_id_key) { return getNode(node_id_key); };

    private:
        Device* _device;
        int _num_nodes = 0;
        int _num_properties_mapped = 0;
};

extern EntityManager* entityManager;

