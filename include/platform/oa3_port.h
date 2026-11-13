#pragma once

#ifdef EBUS_OA3

// The OpenADR 3 VEN (doc/OpenADR3.md): cpp-oa3's VenClient on a FreeRTOS task of its own,
// configured from the oa3: section of config.yml. Call once the network is up; the client
// waits for NTP before its first request.
void setup_oa3();

#endif
