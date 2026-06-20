#pragma once
#include <homie/Device.h>
#include <homie/Node.h>
#include <homie/Property.h>

#include <node/NodeProperty.h>

class NodeEntity {
    public:
        NodeEntity(const char* homie_id);
        virtual ~NodeEntity(){};

        virtual void loop() = 0;        // loop processing (MUST override in derived classes)
        virtual const char* getNodeId() {return _homie_node_id;};

        virtual bool setup();                           // Arduino-style setup - override and chain to base
        virtual bool setup(const char* homie_node_id); // convenience: set ID then setup
        virtual void setHomieNodeId(const char* id);
        virtual bool pollIntervalTrigger();
        virtual const char* getHomieNodeId();

        void setDevice(Device* device) {_device = device;};
        const char* getPropertyTopic(const char* property_id, char* buf, size_t bufSize = 128);
        Node* getNode(const char* id);
        void publish(const char* node_id);
        Property* validate(const char* node_id, const char* property_id);


        virtual void entity_settable_callback(Property* property);
        virtual void entity_settable_callback(const char* property_id, const char* value);
        virtual void set(bool onOff) {};
        virtual void set(int val) {};
        virtual void set(float val) {};
        virtual void set(const char* val) {};

    protected:
        Node* _parent_node = nullptr; // Pointer to the parent Node
        char _homie_node_id[32] = {0}; // Homie node ID,
        NodeProperty* _node_properties[32]; // Array of NodeProperties
        int _num_node_properties = 0;

    private:
        Device* _device;
        int _num_nodes = 0;
        int _num_properties_mapped = 0;
};
