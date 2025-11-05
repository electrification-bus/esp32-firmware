#pragma once
#include <homie/Device.h>
#include <homie/Node.h>
#include <homie/Property.h>

#include <node/NodeProperty.h>

class NodeEntity {
    public:
        NodeEntity(){};
        NodeEntity(Device* device) : _device(device) {};
        virtual ~NodeEntity(){};
        virtual bool nodeBegin();       // start the node (MAY be overridden in derived classes)
        virtual void loop() = 0;        // loop processing (MUST override in derived classes)
        virtual const char* getNodeName() const = 0;

        virtual bool setup();
        virtual bool setup(const char* node_id);
        virtual void setHomieNodeId(const char* id);
        virtual bool pollIntervalTrigger();
        virtual const char* getHomieNodeId();

        //from EntityManager (NodeManager)
        void setDevice(Device* device) {_device = device;};
        Node* getNode(const char* id);
        void publish(const char* node_id);
        Property* validate(const char* node_id, const char* property_id);
        void setNodeProperty(const char* property_id, int value);
        void setNodeProperty(const char* property_id, float value);
        void setNodeProperty(const char* property_id, const char* value);
        void setNodeProperty(const char* property_id, bool value);
        void setNodeProperty(const char* property_id, unsigned int value);

        void entity_settable_callback(Property* property);
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
        //from EntityManager
        Device* _device;
        int _num_nodes = 0;
        int _num_properties_mapped = 0;
};