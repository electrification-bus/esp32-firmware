#include <Arduino.h>
#include <ESPmDNS.h>
#include <platform/mdns_backend_esp32.h>
#include <platform/log_stream.h>

// Split "_ebus._tcp" into "_ebus" and "_tcp", the two arguments ESPmDNS takes.
static bool split_type(const char* type, char* service, size_t service_size, char* proto,
                       size_t proto_size) {
  const char* dot = strrchr(type, '.');
  if (dot == nullptr || dot == type || dot[1] == '\0') return false;
  size_t len = (size_t)(dot - type);
  if (len >= service_size || strlen(dot + 1) >= proto_size) return false;
  memcpy(service, type, len);
  service[len] = '\0';
  strcpy(proto, dot + 1);
  return true;
}

static void format_ip(IPAddress ip, char* out, size_t out_size) {
  snprintf(out, out_size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

bool Esp32MdnsBackend::advertise(const char* service_type, uint16_t port, const TxtPair* txt,
                                 uint8_t txt_count) {
  char service[32];
  char proto[8];
  if (!split_type(service_type, service, sizeof(service), proto, sizeof(proto))) return false;
  if (!MDNS.addService(service, proto, port)) return false;
  for (uint8_t i = 0; i < txt_count; i++) {
    MDNS.addServiceTxt(service, proto, txt[i].key, txt[i].value);
  }
  return true;
}

// A browsed instance's TXT pairs, copied out of ESPmDNS's String temporaries: "key\0value\0"
// back to back in _txt_text, pointed into by _txt_pairs. A pair that does not fit is
// dropped. Static, since browse() runs on the main task only.
static char _txt_text[512];
static TxtPair _txt_pairs[EBUS_TXT_MAX_PAIRS];

static uint8_t copy_txt(int idx) {
  uint8_t count = 0;
  size_t used = 0;
  int n = MDNS.numTxt(idx);
  for (int j = 0; j < n && count < EBUS_TXT_MAX_PAIRS; j++) {
    // Copied within the statement, while the String temporary is still alive.
    char key[64];
    char value[256];
    snprintf(key, sizeof(key), "%s", MDNS.txtKey(idx, j).c_str());
    snprintf(value, sizeof(value), "%s", MDNS.txt(idx, j).c_str());
    Serial.printf("mDNS:     TXT %s=%s\n", key, value);
    size_t klen = strlen(key) + 1;
    size_t vlen = strlen(value) + 1;
    if (used + klen + vlen > sizeof(_txt_text)) continue;
    memcpy(_txt_text + used, key, klen);
    memcpy(_txt_text + used + klen, value, vlen);
    _txt_pairs[count].key = _txt_text + used;
    _txt_pairs[count].value = _txt_text + used + klen;
    count++;
    used += klen + vlen;
  }
  return count;
}

int Esp32MdnsBackend::browse(const char* service_type, uint32_t timeout_ms, mdns_instance_fn fn,
                             void* ctx) {
  (void)timeout_ms;
  char service[32];
  char proto[8];
  if (!split_type(service_type, service, sizeof(service), proto, sizeof(proto))) return -1;
  int num_found = MDNS.queryService(service, proto);
  if (num_found <= 0) {
    Serial.printf("mDNS: No %s services found\n", service_type);
    return 0;
  }
  Serial.printf("mDNS: Found %d %s service(s)\n", num_found, service_type);

  int passed = 0;
  for (int i = 0; i < num_found; i++) {
    char host[EBUS_MDNS_HOST_MAX];
    snprintf(host, sizeof(host), "%s", MDNS.hostname(i).c_str());
    uint16_t port = MDNS.port(i);
    Serial.printf("mDNS:   [%d] host=%s port=%u\n", i, host, port);
    uint8_t txt_count = copy_txt(i);

    // Renamed IP() -> address() between arduino-esp32 2.0.x and 3.x's ESPmDNS.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    IPAddress ip = MDNS.address(i);
#else
    IPAddress ip = MDNS.IP(i);
#endif
    char address[EBUS_MDNS_ADDRESS_MAX] = "";
    if (ip == IPAddress(0, 0, 0, 0)) {
      Serial.printf("mDNS:   [%d] No IP resolved, skipping\n", i);
    } else {
      format_ip(ip, address, sizeof(address));
    }

    MdnsInstance inst = {"", host, address, port, _txt_pairs, txt_count};
    passed++;
    if (!fn(ctx, &inst)) break;
  }
  return passed;
}

bool Esp32MdnsBackend::resolve(const char* hostname, uint32_t timeout_ms, char* address,
                               size_t address_size) {
  IPAddress ip = MDNS.queryHost(hostname, timeout_ms);
  if (ip == IPAddress(0, 0, 0, 0)) return false;
  format_ip(ip, address, address_size);
  return true;
}
