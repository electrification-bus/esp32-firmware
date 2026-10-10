#pragma once
// logic/link: a device.yml node (or controller links: entry) that runs one EbusLink, the
// core's property link. Behavior, reference forms and limits: doc/Link.md and
// <ebus/link/link.h>.
#include <node/NodeEntity.h>
#include <ebus/link/link.h>

class Link : public NodeEntity {
    public:
        Link(const char* homie_node_id, const char* source, const char* target,
             const char* format, int decimals, uint32_t interval_ms)
            : NodeEntity(homie_node_id),
              _link(_homie_node_id, source, target, format, decimals, interval_ms) {}
        ~Link(){};

        bool setup() override;
        void loop() override;

    private:
        EbusLink _link;
};
