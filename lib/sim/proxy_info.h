#pragma once
#include <node/NodeEntity.h>

// Proxy `info` node (G1 / proxy.md): publishes the `info` capability that marks a device
// as PROXIED. `proxied=true` is the explicit disambiguation signal (the secondary
// mechanism — the primary is the root device's type); `serial-number` lets a consumer
// correlate this proxied representation with a future native one. Static metadata,
// published once at setup(). Demo values — a real proxy populates serial/vendor from the
// proxied device's identity (vendor API, commissioning data).
class ProxyInfo : public NodeEntity {
public:
    ProxyInfo(const char* homie_node_id) : NodeEntity(homie_node_id) {}
    bool setup() override;
    void loop() override {}
private:
    NodeProperty _proxied, _serial, _vendor;
};
