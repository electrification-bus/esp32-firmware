#pragma once
#include <stddef.h>
#include <stdint.h>

// Arduino-free pieces of mDNS broker discovery, shared by the firmware (config.cpp,
// network.cpp, mqtt_client.cpp) and the host tests in test/native/test_broker_discovery.

// Broker service types from the eBus specification (framework.md, "MQTT Broker
// Advertisement" and "Broker Discovery"). The spelling is the DNS-SD service name without
// the leading underscore and the _tcp protocol, as device.yml's mqtt.discover takes it.
enum BrokerService {
  BROKER_SVC_SECURE_MQTT = 0,   // _secure-mqtt._tcp: MQTT over TLS, 8883
  BROKER_SVC_MQTT,              // _mqtt._tcp: plain MQTT, 1883
  BROKER_SVC_MQTT_WS,           // _mqtt-ws._tcp: MQTT over WebSocket, 9001
  BROKER_SVC_MQTT_WSS,          // _mqtt-wss._tcp: MQTT over secure WebSocket, 9002
  BROKER_SVC_COUNT
};

// Service name ("secure-mqtt"), or "" for an out-of-range value.
const char* broker_service_name(uint8_t svc);
// Port used when the SRV record carries none.
uint16_t broker_service_default_port(uint8_t svc);
// True when the MQTT client can connect over this transport. It is a TCP/TLS client, so
// the WebSocket transports are known names that it cannot use.
bool broker_service_supported(uint8_t svc);
// Service for a name, or -1 when the name is not one of the four.
int broker_service_parse(const char* name);

// The service types the querier browses, in priority order.
struct BrokerDiscoveryList {
  uint8_t count;
  uint8_t services[BROKER_SVC_COUNT];
};

// secure-mqtt, then mqtt.
void broker_discovery_default(BrokerDiscoveryList* list);
void broker_discovery_clear(BrokerDiscoveryList* list);

enum BrokerDiscoveryAdd {
  BROKER_ADD_OK = 0,
  BROKER_ADD_UNKNOWN,       // not a broker service name
  BROKER_ADD_UNSUPPORTED,   // a WebSocket transport
  BROKER_ADD_DUPLICATE      // already in the list
};

// Append a service by name. The list is left unchanged unless the result is BROKER_ADD_OK.
BrokerDiscoveryAdd broker_discovery_add(BrokerDiscoveryList* list, const char* name);

// Copy a discovered broker's mDNS host name into out without a trailing ".local" or
// ".local." (the querier takes the bare name). Returns false, with out empty, when the
// name is empty or does not fit.
bool broker_copy_hostname(const char* in, char* out, size_t out_size);

// Consecutive failed reconnects to a discovered broker's address before its host name is
// resolved again. The reconnect interval is 10 s, so about 30 s.
#define BROKER_RERESOLVE_AFTER_FAILURES 3

struct BrokerReconnect {
  uint8_t failures;
};

// A connect succeeded.
void broker_reconnect_succeeded(BrokerReconnect* state);
// A reconnect failed. Returns true on every BROKER_RERESOLVE_AFTER_FAILURES-th consecutive
// failure, when the caller should resolve the host name again.
bool broker_reconnect_failed(BrokerReconnect* state);
