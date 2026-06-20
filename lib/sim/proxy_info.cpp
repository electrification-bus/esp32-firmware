#include <Arduino.h>
#include <sim/proxy_info.h>

bool ProxyInfo::setup() {
    NodeEntity::setup();   // active device context (A2) — publishes under the child device
    _proxied.setup(getNodeId(), "proxied");
    _serial.setup(getNodeId(), "serial-number");
    _vendor.setup(getNodeId(), "vendor-name");
    // Static identity for this proxied device. proxied=true is the whole point.
    _proxied.set(true);                _proxied.publish();
    _serial.set("PROXY-DEMO-SN-001");  _serial.publish();
    _vendor.set("Acme Energy");        _vendor.publish();
    Serial.printf("ProxyInfo: published info (proxied=true) on node '%s'\n", getNodeId());
    return true;
}
