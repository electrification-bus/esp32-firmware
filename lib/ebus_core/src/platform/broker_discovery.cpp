#include <platform/broker_discovery.h>
#include <string.h>

struct BrokerServiceInfo {
  const char* name;
  uint16_t default_port;
  bool supported;
};

static const BrokerServiceInfo BROKER_SERVICES[BROKER_SVC_COUNT] = {
  {"secure-mqtt", 8883, true},
  {"mqtt",        1883, true},
  {"mqtt-ws",     9001, false},
  {"mqtt-wss",    9002, false},
};

const char* broker_service_name(uint8_t svc) {
  return svc < BROKER_SVC_COUNT ? BROKER_SERVICES[svc].name : "";
}

uint16_t broker_service_default_port(uint8_t svc) {
  return svc < BROKER_SVC_COUNT ? BROKER_SERVICES[svc].default_port : 0;
}

bool broker_service_supported(uint8_t svc) {
  return svc < BROKER_SVC_COUNT && BROKER_SERVICES[svc].supported;
}

int broker_service_parse(const char* name) {
  if (name == nullptr) return -1;
  for (int i = 0; i < BROKER_SVC_COUNT; i++) {
    if (strcmp(name, BROKER_SERVICES[i].name) == 0) return i;
  }
  return -1;
}

void broker_discovery_clear(BrokerDiscoveryList* list) {
  list->count = 0;
}

void broker_discovery_default(BrokerDiscoveryList* list) {
  list->count = 2;
  list->services[0] = BROKER_SVC_SECURE_MQTT;
  list->services[1] = BROKER_SVC_MQTT;
}

BrokerDiscoveryAdd broker_discovery_add(BrokerDiscoveryList* list, const char* name) {
  int svc = broker_service_parse(name);
  if (svc < 0) return BROKER_ADD_UNKNOWN;
  if (!broker_service_supported((uint8_t)svc)) return BROKER_ADD_UNSUPPORTED;
  for (uint8_t i = 0; i < list->count; i++) {
    if (list->services[i] == svc) return BROKER_ADD_DUPLICATE;
  }
  // Four distinct names at most, so a list that got here has room.
  list->services[list->count++] = (uint8_t)svc;
  return BROKER_ADD_OK;
}

static bool ends_with(const char* s, size_t len, const char* suffix) {
  size_t n = strlen(suffix);
  return len >= n && strcmp(s + len - n, suffix) == 0;
}

bool broker_copy_hostname(const char* in, char* out, size_t out_size) {
  if (out_size == 0) return false;
  out[0] = '\0';
  if (in == nullptr) return false;
  size_t len = strlen(in);
  if (ends_with(in, len, ".local.")) len -= 7;
  else if (ends_with(in, len, ".local")) len -= 6;
  if (len == 0 || len >= out_size) return false;
  memcpy(out, in, len);
  out[len] = '\0';
  return true;
}

void broker_reconnect_succeeded(BrokerReconnect* state) {
  state->failures = 0;
}

bool broker_reconnect_failed(BrokerReconnect* state) {
  state->failures++;
  if (state->failures < BROKER_RERESOLVE_AFTER_FAILURES) return false;
  state->failures = 0;
  return true;
}
