#pragma once

class Entity {
    public:
        Entity();
        virtual ~Entity();
        virtual bool nodeBegin() = 0;  // start the node (MUST override in derived classes)
        virtual void loop() = 0;        // loop processing (MUST override in derived classes)
        virtual const char* getNodeName() const = 0;

        virtual bool setup();
        virtual bool setup(const char* node_id);
        virtual void setHomieNodeId(const char* id);
        virtual bool pollIntervalTrigger();
        virtual const char* getHomieNodeId();
        virtual const char* getClassName() const;

    protected:
        char homie_node_id[32] = {0}; // Homie node ID,

};