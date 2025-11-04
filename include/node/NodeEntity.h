#pragma once
#include <node/NodeProperty.h>

class NodeEntity {
    public:
        NodeEntity();
        virtual ~NodeEntity();
        virtual bool nodeBegin();  // start the node (MUST override in derived classes)
        virtual void loop() = 0;        // loop processing (MUST override in derived classes)
        virtual const char* getNodeName() const = 0;

        virtual bool setup();
        virtual bool setup(const char* node_id);
        virtual void setHomieNodeId(const char* id);
        virtual bool pollIntervalTrigger();
        virtual const char* getHomieNodeId();
        virtual const char* getClassName() const {return "NodeEntity";}

    protected:
        Node* _parent_node = nullptr; // Pointer to the parent Node
        char _homie_node_id[32] = {0}; // Homie node ID,
        NodeProperty* _node_properties[32]; // Array of NodeProperties
        int _num_node_properties = 0;
};