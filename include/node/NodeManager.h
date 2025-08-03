#pragma once
#include <ArduinoJson.h>
#include <homie/Device.h>
#include <homie/Node.h>
#include <node/BME280.h>

class NodeManager {
    public:
        NodeManager(){};
        ~NodeManager(){};

        void setup();
        void loop();
        void addNode(Node* node);
        void removeNode(const char* id);
        Node* getNode(const char* id);
        void publish(const char* node_id);
        void setNodeProperty(const char* node_id, const char* property_id, int value);
        void setNodeProperty(const char* node_id, const char* property_id, float value);
        void setNodeProperty(const char* node_id, const char* property_id, const char* value);
        void setNodeProperty(const char* node_id, const char* property_id, bool value);
        void setNodeProperty(const char* node_id, const char* property_id, unsigned int value);
    private:
        Node* _nodes[32];
        int _num_nodes = 0;
        int _num_properties_mapped = 0;
};

extern NodeManager* nodeManager;

