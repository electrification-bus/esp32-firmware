#pragma once
// MdnsBackend (ebus/discovery/mdns_backend.h) over arduino-esp32's ESPmDNS. setup_mdns()
// claims the host name with MDNS.begin() before anything uses it.
#include <ebus/discovery/mdns_backend.h>

class Esp32MdnsBackend final : public MdnsBackend {
 public:
  // MDNS.addService(), then MDNS.addServiceTxt() for each pair, in order.
  bool advertise(const char* service_type, uint16_t port, const TxtPair* txt,
                 uint8_t txt_count) override;
  // MDNS.queryService(), which waits its own fixed 3 s: timeout_ms is not used. IPv4 only.
  int browse(const char* service_type, uint32_t timeout_ms, mdns_instance_fn fn,
             void* ctx) override;
  // MDNS.queryHost(). IPv4 only.
  bool resolve(const char* hostname, uint32_t timeout_ms, char* address,
               size_t address_size) override;
};
