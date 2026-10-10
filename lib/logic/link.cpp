#include <logic/link.h>

bool Link::setup() {
#ifdef ENABLE_CONTROLLER
    ebus_link_enable_controller();   // links the controller-mode code (cpp-sdk 0.6.0)
    return _link.setup(EbusLink::CONTROLLER);
#else
    NodeEntity::setup();
    return _link.setup(EbusLink::DEVICE, &theDevice);
#endif
}

void Link::loop() {
    _link.loop();
}
