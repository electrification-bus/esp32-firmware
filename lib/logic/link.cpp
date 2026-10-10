#include <logic/link.h>

bool Link::setup() {
#ifdef ENABLE_CONTROLLER
    return _link.setup(EbusLink::CONTROLLER);
#else
    NodeEntity::setup();
    return _link.setup(EbusLink::DEVICE, &theDevice);
#endif
}

void Link::loop() {
    _link.loop();
}
