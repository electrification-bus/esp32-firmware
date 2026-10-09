#pragma once
// The services setup_mdns() advertises, built with the core's TXT builders
// (ebus/discovery/txt_records.h) and registered through an MdnsBackend. No Arduino
// dependency, so test/native/test_mdns_services runs it on the host against a recording
// backend.
#include <stdint.h>
#include <ebus/discovery/mdns_backend.h>
#include <ebus/discovery/txt_records.h>

// What network.cpp reads from the configuration and the platform.
struct MdnsServicesConfig {
  const char* device_id;      // getDeviceID()
  const char* homie_role;     // "controller" advertises controller; anything else, device
  const char* manufacturer;
  const char* model;
  const char* serial;         // device.serial_number; "" falls back to device_id
  const char* device_type;
  const char* name;
  const char* fw_version;
  const char* os_version;
  const char* mac;            // lowercase hex without colons
  uint16_t http_port;         // _device-info._tcp and _http._tcp
  uint16_t ebus_port;         // _ebus._tcp: mqtt.port
  uint16_t log_port;          // _telnet._tcp; 0 when the log stream is not listening
};

// In advertising order.
enum MdnsService {
  MDNS_SERVICE_DEVICE_INFO = 0,
  MDNS_SERVICE_HTTP,
  MDNS_SERVICE_LOG,
  MDNS_SERVICE_EBUS,
  MDNS_SERVICE_COUNT
};

enum MdnsServiceStatus {
  MDNS_SERVICE_ADVERTISED = 0,
  MDNS_SERVICE_SKIPPED,       // _telnet._tcp with log_port 0
  MDNS_SERVICE_BUILD_FAILED,  // the TXT builder refused the identity (TxtResult in txt)
  MDNS_SERVICE_REFUSED        // the backend refused the service
};

struct MdnsServicesResult {
  uint8_t status[MDNS_SERVICE_COUNT];   // MdnsServiceStatus
  uint8_t txt[MDNS_SERVICE_COUNT];      // TxtResult of the build
};

// The full DNS-SD type of a service ("_ebus._tcp").
const char* mdns_service_type(uint8_t service);

// Fill id from cfg. Every pointer in id refers to cfg's strings or to constants.
void mdns_services_identity(const MdnsServicesConfig* cfg, EbusIdentity* id);

// Build and advertise every service, in MdnsService order.
void mdns_services_advertise(MdnsBackend* backend, const MdnsServicesConfig* cfg,
                             MdnsServicesResult* result);
