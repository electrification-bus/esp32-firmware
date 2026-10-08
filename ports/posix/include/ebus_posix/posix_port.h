#pragma once
// The POSIX port's bindings for the core (lib/ebus_core/README.md, "What a port
// implements"): the clock, the console and the settable table's storage.
#include <homie/homie_settable.h>
#include <homie/homie_transport.h>

class Property;

#ifndef EBUS_POSIX_SETTABLE_CAPACITY
#define EBUS_POSIX_SETTABLE_CAPACITY 64
#endif

// Bind the clock (CLOCK_MONOTONIC, nanosleep) and the log sink (stderr, or nothing when
// `quiet`), and give the settable table EBUS_POSIX_SETTABLE_CAPACITY static entries bound
// to `transport`. Call once, before anything registers a /set topic. There is no
// NodeEntity on this port, so no driver call is bound: a settable reaches the application
// through a settable_handler_t.
void ebus_posix_port_init(HomieTransport* transport, bool quiet);

// Register `property`'s /set topic in the settable table: the payload is validated and
// stored on the property, then `handler` (may be null) accepts or refuses it. Works before
// the first connect; the transport subscribes every registered topic on each connect.
// Returns false if the topic does not fit.
bool ebus_posix_register_settable(Property* property, settable_handler_t handler, void* ctx);
