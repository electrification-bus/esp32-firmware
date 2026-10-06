#include <Arduino.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <esp_mac.h>
#include <lwip/netif.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#ifndef CONFIG_WIFI_AP
#include <ETH.h>
#endif
#include <time.h>
#include <platform/config.h>
#include <platform/network.h>
#include <homie/homie.h>
#include <platform/mdns_strings.h>
#include <platform/log_stream.h>

// Forward declaration to avoid circular include with mqtt_client.h
extern boolean mqtt_connected();

#define WIFI_SSID "foo"
#define WIFI_PW   "bar"

int CONNECT_ATTEMPTS = 6;
WiFiMulti wifiMulti;
#ifndef CONFIG_WIFI_AP
static bool eth_connected = false;
#endif

// Static buffer for sanitized hostname (shared across functions)
static char mdns_hostname[64] = {0};

bool client_connected() {
#ifndef CONFIG_WIFI_AP
  return eth_connected || (WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA)));
#else
  return WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA));
#endif
}

// Static buffers for IP address strings
static char wifi_ip_str[16] = {0};
static char eth_ip_str[16] = {0};

// Function to get the WiFi IP address as a string
const char* get_wifi_ip() {
  if (WiFi.isConnected()) {
    IPAddress ip = WiFi.localIP();
    snprintf(wifi_ip_str, sizeof(wifi_ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return wifi_ip_str;
  }
  return "";
}

// Function to get the Ethernet IP address as a string
const char* get_eth_ip() {
#ifndef CONFIG_WIFI_AP
  if (eth_connected) {
    IPAddress ip = ETH.localIP();
    snprintf(eth_ip_str, sizeof(eth_ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return eth_ip_str;
  }
#endif
  return "";
}

// Get the mDNS hostname (sanitized device name)
const char* get_mdns_hostname() {
  return mdns_hostname;
}

// Static buffers for MAC address strings
static char eth_mac_str[18] = {0};
static char wlan_mac_str[18] = {0};

const char* get_eth_mac() {
  if (eth_mac_str[0] == '\0') {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_ETH);
    snprintf(eth_mac_str, sizeof(eth_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  return eth_mac_str;
}

const char* get_wlan_mac() {
  if (wlan_mac_str[0] == '\0') {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(wlan_mac_str, sizeof(wlan_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  return wlan_mac_str;
}

// Convert device name to valid mDNS hostname (lowercase, alphanumeric, hyphens)
static void sanitize_hostname(const char* input, char* output, size_t max_len) {
  size_t j = 0;
  for (size_t i = 0; input[i] && j < max_len - 1; i++) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') {
      output[j++] = c + 32;  // lowercase
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      output[j++] = c;
    } else if (c == ' ' || c == '_') {
      output[j++] = '-';  // convert spaces/underscores to hyphens
    }
    // skip other characters
  }
  output[j] = '\0';
}

// Helper function to add mDNS service TXT record (resolves overload ambiguity)
static void mdns_add_txt(const char* service, const char* proto, const char* key, const char* value) {
  MDNS.addServiceTxt(service, proto, key, value);
}

// Forward declaration - registers all mDNS services
void mdns_register_services();

// mDNS-discovered MQTT broker (separate from config broker)
static char mdns_broker_host[64] = {0};
static uint16_t mdns_broker_port = 0;

#define MDNS_DISCOVERY_ATTEMPTS 3
#define MDNS_DISCOVERY_DELAY_MS 3000

// Browse one DNS-SD service type; on the first result with a resolvable IP, store the
// broker in mdns_broker_host/port and return true. default_port is used when the SRV
// record carries no port. Does not modify mqtt_host/mqtt_port globals.
static bool mdns_query_one_service(const char* service, uint16_t default_port) {
  int num_found = MDNS.queryService(service, MDNS_QUERY_TCP);
  if (num_found <= 0) {
    Serial.printf("mDNS: No _%s._tcp services found\n", service);
    return false;
  }

  Serial.printf("mDNS: Found %d _%s._tcp broker(s)\n", num_found, service);

  for (int i = 0; i < num_found; i++) {
    char hostname[64];
    snprintf(hostname, sizeof(hostname), "%s", MDNS.hostname(i).c_str());
    uint16_t svc_port = MDNS.port(i);
    Serial.printf("mDNS:   [%d] host=%s port=%u\n", i, hostname, svc_port);

    // Log TXT records for debugging
    int txt_count = MDNS.numTxt(i);
    for (int j = 0; j < txt_count; j++) {
      Serial.printf("mDNS:     TXT %s=%s\n",
                     MDNS.txtKey(i, j).c_str(), MDNS.txt(i, j).c_str());
    }

    // Use the broker's IP address directly. Renamed IP() -> address() between
    // arduino-esp32 2.0.x and 3.x's ESPmDNS.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    IPAddress broker_ip = MDNS.address(i);
#else
    IPAddress broker_ip = MDNS.IP(i);
#endif
    if (broker_ip == IPAddress(0, 0, 0, 0)) {
      Serial.printf("mDNS:   [%d] No IP resolved, skipping\n", i);
      continue;
    }

    snprintf(mdns_broker_host, sizeof(mdns_broker_host), "%d.%d.%d.%d",
             broker_ip[0], broker_ip[1], broker_ip[2], broker_ip[3]);
    mdns_broker_port = svc_port > 0 ? svc_port : default_port;

    Serial.printf("mDNS: Discovered %s broker: %s:%u (host=%s)\n",
                   mdns_broker_port == MQTT_SECURE_PORT ? "MQTTS" : "MQTT",
                   mdns_broker_host, mdns_broker_port, hostname);
    return true;
  }
  return false;
}

// Discover an MQTT broker via mDNS, storing the result in mdns_broker_host/port.
// Prefers _secure-mqtt._tcp (TLS, 8883) — the eBus-primary form that TLS-only brokers
// (e.g. SPAN) advertise — and falls back to _mqtt._tcp (plain, 1883), which the eBus
// spec only expects when TLS isn't feasible.
static bool mdns_query_mqtt_broker() {
  Serial.println("mDNS: Searching for MQTT brokers (_secure-mqtt._tcp, then _mqtt._tcp)...");

  for (int attempt = 0; attempt < MDNS_DISCOVERY_ATTEMPTS; attempt++) {
    if (attempt > 0) {
      Serial.printf("mDNS: Discovery attempt %d of %d...\n", attempt + 1, MDNS_DISCOVERY_ATTEMPTS);
      delay(MDNS_DISCOVERY_DELAY_MS);
    }

    if (mdns_query_one_service(MDNS_QUERY_SECURE_MQTT, MQTT_SECURE_PORT)) return true;
    if (mdns_query_one_service(MDNS_QUERY_MQTT, 1883)) return true;
  }

  Serial.println("mDNS: No MQTT broker found via mDNS");
  return false;
}

const char* mdns_get_broker_host() {
  return mdns_broker_host;
}

uint16_t mdns_get_broker_port() {
  return mdns_broker_port;
}

bool mdns_has_broker() {
  return mdns_broker_host[0] != '\0';
}

bool setup_mdns() {
  // mDNS split into two independent gates (ESP32-6du.6): mdns_discovery (querier —
  // find the broker, crash-safe, high value) and mdns_advertise (responder — register
  // service records, the ESP32-9mx crash surface). Defaults are build-conditional —
  // see the #if in config.cpp.
  if (!mdns_discovery && !mdns_advertise) {
    Serial.println("mDNS: disabled (mdns_discovery=false, mdns_advertise=false)");
    return false;
  }

  // Use device name as hostname (sanitized for mDNS); fall back to device ID.
  char base_hostname[48] = {0};
  if (strlen(device_name) > 0) {
    sanitize_hostname(device_name, base_hostname, sizeof(base_hostname));
  } else {
    strncpy(base_hostname, getDeviceID(), sizeof(base_hostname) - 1);
  }
  strncpy(mdns_hostname, base_hostname, sizeof(mdns_hostname) - 1);

  // The Arduino MDNS API couples mdns_init() with a hostname claim, so MDNS.begin()
  // is required even for the querier. When advertising, defend a UNIQUE hostname by
  // probing for conflicts and appending a suffix. When only discovering, the hostname
  // is cosmetic and no services are registered, so a single begin() is enough — that
  // keeps the responder surface minimal (no service/TXT records, the main crash path).
  if (mdns_advertise) {
    for (int suffix = 2; suffix <= 99; suffix++) {
      if (MDNS.begin(mdns_hostname)) {
        // Check for conflict: query our own hostname and see if another device responds
        IPAddress resolved = MDNS.queryHost(mdns_hostname, 2000);
        if (resolved == IPAddress(0, 0, 0, 0)) {
          break;  // No conflict, hostname is ours
        }
        // Check if the resolved IP is one of our own interfaces
        IPAddress our_wifi = WiFi.localIP();
#ifndef CONFIG_WIFI_AP
        IPAddress our_eth = ETH.localIP();
        if (resolved == our_eth || resolved == our_wifi) {
#else
        if (resolved == our_wifi) {
#endif
          break;  // It's us, no conflict
        }
        Serial.printf("mDNS: Hostname '%s.local' conflict (taken by %d.%d.%d.%d), trying next\n",
                       mdns_hostname, resolved[0], resolved[1], resolved[2], resolved[3]);
        MDNS.end();
      }
      snprintf(mdns_hostname, sizeof(mdns_hostname), "%s-%d", base_hostname, suffix);
    }
  } else {
    MDNS.begin(mdns_hostname);  // querier-only: init + minimal hostname, no service records
  }
  Serial.printf("mDNS: Started for host: %s.local (discovery=%d advertise=%d)\n",
                 mdns_hostname, (int)mdns_discovery, (int)mdns_advertise);

  // Querier: discover MQTT brokers on the LAN (skip in configured-only mode).
  if (mdns_discovery && mqtt_mode != MQTT_MODE_CONFIGURED_ONLY) {
    mdns_query_mqtt_broker();
  }

  // Responder: advertise our service records (the ESP32-9mx crash surface).
  if (mdns_advertise) {
    mdns_register_services();
  }

  return true;
}

// Register all mDNS services - called from setup_mdns() and after WiFi STA connects
void mdns_register_services() {
  // ============================================================
  // _device-info._tcp service - Hardware/firmware information
  // ============================================================
  MDNS.addService(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, 80);
  if (strlen(device_manufacturer) > 0) {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_MANUFACTURER, device_manufacturer);
  }
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_MODEL, device_model);
  if (strlen(device_serial) > 0) {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_SERIAL_NUMBER, device_serial);
  } else {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_SERIAL_NUMBER, getDeviceID());
  }
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_ETH_MAC, get_eth_mac());
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_WLAN_MAC, get_wlan_mac());
  if (strlen(firmware_name) > 0) {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_FW_NAME, firmware_name);
  }
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_FW_VERSION, firmware_version);
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_PLATFORM, MDNS_VAL_PLATFORM);
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_SDK_VERSION, ESP.getSdkVersion());

  const char* eth_ip = get_eth_ip();
  if (strlen(eth_ip) > 0) {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_ETH_IP, eth_ip);
  }
  const char* wifi_ip = get_wifi_ip();
  if (strlen(wifi_ip) > 0) {
    mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_WLAN_IP, wifi_ip);
  }
  Serial.printf("mDNS: Added _device-info._tcp service\n");

  // ============================================================
  // _http._tcp service - HTTP API with OpenAPI specification
  // ============================================================
  MDNS.addService(MDNS_SVC_HTTP, MDNS_PROTO_TCP, 80);
  mdns_add_txt(MDNS_SVC_HTTP, MDNS_PROTO_TCP, MDNS_TXT_HTTP_PATH, MDNS_VAL_HTTP_PATH);
  mdns_add_txt(MDNS_SVC_HTTP, MDNS_PROTO_TCP, MDNS_TXT_HTTP_API, MDNS_VAL_HTTP_API);
  Serial.printf("mDNS: Added _http._tcp service (path=%s)\n", MDNS_VAL_HTTP_PATH);

  // ============================================================
  // _telnet._tcp service - serial log stream, so a monitor can find the device
  // by name instead of by IP. Only advertised when the listener is actually up.
  // ============================================================
  if (log_tcp_running()) {
    MDNS.addService(MDNS_SVC_LOG, MDNS_PROTO_TCP, log_tcp_get_port());
    mdns_add_txt(MDNS_SVC_LOG, MDNS_PROTO_TCP, MDNS_TXT_LOG_KIND, MDNS_VAL_LOG_KIND);
    Serial.printf("mDNS: Added _telnet._tcp service (serial log, port %u)\n", log_tcp_get_port());
  }

  // ============================================================
  // _homie._tcp service - Homie protocol device/controller discovery
  // ============================================================
  MDNS.addService(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, mqtt_port);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_VERSION, MDNS_VAL_HOMIE_VERSION);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_DOC_VER, MDNS_VAL_HOMIE_DOC_VER);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_ID, getDeviceID());
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_NAME, (const char*)device_name);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_ROLE, (const char*)homie_role);
  if (strlen(device_type) > 0) {
    MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_TYPE, (const char*)device_type);
  }
  char base_topic[96] = {0};
  snprintf(base_topic, sizeof(base_topic), "%s/%s/", HOMIE_TOPIC_PREFIX, getDeviceID());
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_BASE, (const char*)base_topic);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_STATE, HOMIE_STATE_INIT);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_HOMIE_IMPL, MDNS_VAL_HOMIE_IMPL);
  MDNS.addServiceTxt(MDNS_SVC_HOMIE, MDNS_PROTO_TCP, MDNS_TXT_CA_CERT_PATH, MDNS_VAL_CA_CERT_PATH);
  Serial.printf("mDNS: Added _ebus._tcp service (role=%s, base=%s, ca_cert_path=%s)\n", homie_role, base_topic, MDNS_VAL_CA_CERT_PATH);
}

bool setup_wifi() {
  Serial.printf("wifi connecting: %s\n", WIFI_SSID);
  wifiMulti.addAP(WIFI_SSID, WIFI_PW);
  while (wifiMulti.run() != WL_CONNECTED && (CONNECT_ATTEMPTS-- > 0)) {
    delay(1000);
    Serial.printf("wifi failed to connect - retrying %s\n", WIFI_SSID);
  }
  bool didConnect = client_connected();
  Serial.printf("WiFi: %s: %s\n", WIFI_SSID, didConnect ?  WiFi.localIP().toString().c_str() : "FAILED TO CONNECT");
  return didConnect;
}


/******************** Ethernet and SD card ****************** */
/*
    This sketch is combined demo of: 1) SD(esp_32) -- > SD_Test; 2) WiFi --> ETH_LAN8720
    It displays info about the card on the terminal and initializes LAN after that. You can ping the IP address shown on the terminal.
*/

#include "FS.h"
//#include "SD_MMC.h"

// ── SNTP start ────────────────────────────────────────────────────────────────
//
// SNTP gets server ADDRESSES, never names, so it never has a DNS lookup in flight.
// On arduino-esp32 3.x, the first NetworkManager::hostByName() after an interface
// gains or loses an address calls dns_clear_cache() without the lwIP core lock. The
// clear runs each pending lookup's callback and closes its UDP pcb on the caller's
// task, and both assert "Required to lock TCPIP core functionality!" (SNTP's callback
// in udp_new_ip_type, the pcb close in udp_remove). lwIP's dns_gethostbyname() returns
// a literal address synchronously with no pending entry. 2.0.x (no cache clear) runs
// the same code.
//
// The names are looked up asynchronously: dns_gethostbyname() runs on the tcpip thread
// (tcpip_callback) and its callback stores the result, which loop_ntp() collects on a
// later pass, so a lookup never blocks the main loop. Our own lookup must not be
// pending at a cache clear either, so every main-task hostByName() caller (MQTT
// connect, the TLS root-cert fetch, OTA pull) calls ntp_lookup_wait() first.
//
// A new lease starts a lookup round of every name. A name that fails keeps its last
// address, if it has one, and SNTP runs on whatever addresses exist. While any name
// fails, only the failed names are looked up again, with exponential backoff. Every
// name is looked up again once NTP_REFRESH_MS has passed since the last round of every
// name, since pool.ntp.org members rotate; a retry due after that point looks up every
// name, so a name that never resolves does not hold the others at their first address.
// SNTP is restarted only when the address set changed.
#define NTP_RETRY_MIN_MS        30000UL
#define NTP_RETRY_MAX_MS        (30UL * 60 * 1000)
#define NTP_REFRESH_MS          (12UL * 60 * 60 * 1000)
#define NTP_LOOKUP_WAIT_MAX_MS  40000UL   // above lwIP's own give-up time for a query
static const char* const NTP_SERVER_NAMES[] = { "pool.ntp.org", "time.nist.gov" };
#define NTP_SERVER_COUNT (sizeof(NTP_SERVER_NAMES) / sizeof(NTP_SERVER_NAMES[0]))

enum { NTP_DNS_IDLE, NTP_DNS_PENDING, NTP_DNS_OK, NTP_DNS_FAIL };   // IDLE: not looked up this round
// Written by the tcpip thread, read by the main task, both under _ntp_dns_mux.
static portMUX_TYPE _ntp_dns_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t _ntp_dns_state[NTP_SERVER_COUNT];
static uint32_t _ntp_dns_addr[NTP_SERVER_COUNT];   // IPv4, network byte order

static volatile bool _ntp_start_pending = false;   // set on every GOT_IP
static bool _ntp_round_active = false;
static bool _ntp_next_armed = false;
static bool _ntp_next_all = false;      // next round looks up every name, not just failed ones
static unsigned long _ntp_next_at = 0;
static unsigned long _ntp_all_at = 0;   // millis() when the last round of every name started
static unsigned long _ntp_retry_ms = NTP_RETRY_MIN_MS;
static bool _ntp_started = false;
// sntp_setservername() keeps the pointers, and SNTP's tcpip-thread requests read them
// while it runs. Two sets: a round writes the set SNTP is not using, and configTime()
// switches SNTP to it under the core lock. Indexed by NTP_SERVER_NAMES; "" is no address.
static char _ntp_server_ip[2][NTP_SERVER_COUNT][16];
static uint8_t _ntp_ip_set = 0;   // the set SNTP holds pointers into

// tcpip thread (or a synchronous result from it).
static void ntp_dns_store(size_t i, const ip_addr_t* addr) {
  bool ok = addr != nullptr && IP_IS_V4(addr);
  portENTER_CRITICAL(&_ntp_dns_mux);
  _ntp_dns_addr[i] = ok ? ip4_addr_get_u32(ip_2_ip4(addr)) : 0;
  _ntp_dns_state[i] = ok ? NTP_DNS_OK : NTP_DNS_FAIL;
  portEXIT_CRITICAL(&_ntp_dns_mux);
}

static void ntp_dns_found(const char* name, const ip_addr_t* addr, void* arg) {
  (void)name;
  ntp_dns_store((size_t)arg, addr);
}

static void ntp_dns_start(void* arg) {
  size_t i = (size_t)arg;
  ip_addr_t addr;
#if LWIP_IPV4 && LWIP_IPV6
  err_t err = dns_gethostbyname_addrtype(NTP_SERVER_NAMES[i], &addr, ntp_dns_found, arg,
                                         LWIP_DNS_ADDRTYPE_IPV4);
#else
  err_t err = dns_gethostbyname(NTP_SERVER_NAMES[i], &addr, ntp_dns_found, arg);
#endif
  if (err == ERR_OK) ntp_dns_store(i, &addr);           // cached: no callback follows
  else if (err != ERR_INPROGRESS) ntp_dns_store(i, nullptr);
}

static bool ntp_lookups_pending() {
  bool pending = false;
  portENTER_CRITICAL(&_ntp_dns_mux);
  for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
    if (_ntp_dns_state[i] == NTP_DNS_PENDING) pending = true;
  }
  portEXIT_CRITICAL(&_ntp_dns_mux);
  return pending;
}

void ntp_lookup_wait() {
  if (!_ntp_round_active || !ntp_lookups_pending()) return;
  Serial.println("NTP: waiting for the NTP server lookup to finish before resolving");
  unsigned long start = millis();
  while (ntp_lookups_pending()) {
    if (millis() - start > NTP_LOOKUP_WAIT_MAX_MS) {
      Serial.println("NTP: WARNING server lookup still pending, continuing");
      return;
    }
    delay(10);
  }
}

// all: look up every name; otherwise only the names that failed last round.
static void ntp_round_start(bool all) {
  for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
    portENTER_CRITICAL(&_ntp_dns_mux);
    bool lookup = all || _ntp_dns_state[i] == NTP_DNS_FAIL;
    _ntp_dns_state[i] = lookup ? NTP_DNS_PENDING : NTP_DNS_IDLE;
    portEXIT_CRITICAL(&_ntp_dns_mux);
    if (lookup && tcpip_callback(ntp_dns_start, (void*)i) != ERR_OK) ntp_dns_store(i, nullptr);
  }
  if (all) _ntp_all_at = millis();
  _ntp_round_active = true;
}

static void ntp_round_finish() {
  uint8_t set = _ntp_ip_set ^ 1;
  size_t failed = 0;
  bool changed = !_ntp_started;
  for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
    portENTER_CRITICAL(&_ntp_dns_mux);
    uint8_t state = _ntp_dns_state[i];
    uint32_t a = _ntp_dns_addr[i];
    portEXIT_CRITICAL(&_ntp_dns_mux);
    if (state == NTP_DNS_OK) {
      const uint8_t* b = (const uint8_t*)&a;
      snprintf(_ntp_server_ip[set][i], sizeof(_ntp_server_ip[set][i]), "%u.%u.%u.%u",
               b[0], b[1], b[2], b[3]);
      Serial.printf("NTP: %s -> %s\n", NTP_SERVER_NAMES[i], _ntp_server_ip[set][i]);
    } else {
      // Not looked up, or failed: keep the previous address (possibly none).
      strcpy(_ntp_server_ip[set][i], _ntp_server_ip[_ntp_ip_set][i]);
      if (state == NTP_DNS_FAIL) {
        failed++;
        Serial.printf("NTP: %s did not resolve%s\n", NTP_SERVER_NAMES[i],
                      _ntp_server_ip[set][i][0] ? ", keeping its previous address" : "");
      }
    }
    if (strcmp(_ntp_server_ip[set][i], _ntp_server_ip[_ntp_ip_set][i]) != 0) changed = true;
  }

  _ntp_next_armed = true;
  _ntp_next_all = failed == 0;
  if (failed > 0) {
    Serial.printf("NTP: retrying %u unresolved name(s) in %lu s\n", (unsigned)failed,
                  _ntp_retry_ms / 1000);
    _ntp_next_at = millis() + _ntp_retry_ms;
    _ntp_retry_ms = _ntp_retry_ms * 2 > NTP_RETRY_MAX_MS ? NTP_RETRY_MAX_MS : _ntp_retry_ms * 2;
  } else {
    _ntp_retry_ms = NTP_RETRY_MIN_MS;
    _ntp_next_at = millis() + NTP_REFRESH_MS;
  }

  const char* servers[NTP_SERVER_COUNT] = {};
  size_t n = 0;
  for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
    if (_ntp_server_ip[set][i][0]) servers[n++] = _ntp_server_ip[set][i];
  }
  if (n == 0 || !changed) return;

  _ntp_ip_set = set;
  configTime(0, 0, servers[0], n > 1 ? servers[1] : nullptr);
  setenv("TZ", NTP_TIMEZONE, 1);
  tzset();
  _ntp_started = true;
  Serial.printf("NTP: sync started with %u server(s), TZ=%s\n", (unsigned)n, NTP_TIMEZONE);
}

void loop_ntp() {
  if (_ntp_round_active) {
    if (ntp_lookups_pending()) return;
    _ntp_round_active = false;
    ntp_round_finish();
    return;
  }
  if (!client_connected()) return;
  if (_ntp_start_pending) {
    _ntp_start_pending = false;           // a new lease skips any backoff wait
    _ntp_retry_ms = NTP_RETRY_MIN_MS;
    _ntp_next_all = true;
  } else if (!_ntp_next_armed || (long)(millis() - _ntp_next_at) < 0) {
    return;
  }
  _ntp_next_armed = false;
  bool all = _ntp_next_all;
  if (!all && millis() - _ntp_all_at >= NTP_REFRESH_MS) {
    Serial.println("NTP: refresh due, looking up every name");
    all = true;
  }
  ntp_round_start(all);
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
#ifndef CONFIG_WIFI_AP
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      if (strlen(mdns_hostname) > 0) {
        ETH.setHostname(mdns_hostname);
      } else if (strlen(device_name) > 0) {
        char hostname[64] = {0};
        sanitize_hostname(device_name, hostname, sizeof(hostname));
        ETH.setHostname(hostname);
      } else {
        ETH.setHostname(getDeviceID());
      }
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      // Make Ethernet the default route (preferred over WiFi for MQTT).
      //
      // This runs on the "arduino_events" task, which does NOT hold the lwIP TCPIP core
      // lock. ESP-IDF 5.x enables CONFIG_LWIP_CHECK_THREAD_SAFETY, so calling a raw lwIP
      // API from here aborts the chip:
      //     assert failed: netif_set_default ... (Required to lock TCPIP core functionality!)
      // — on every DHCP lease, i.e. a boot loop. IDF 4.x had no such check, which is why
      // the netif_list walk below survived unnoticed on the 2.0.x platform.
      // ETH.setDefault() wraps esp_netif_set_default_netif(), which does the locking
      // itself; it only exists in arduino-esp32 3.x, hence the version split.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      if (ETH.setDefault()) {
        Serial.println("ETH: Set as default route (preferred for MQTT)");
      } else {
        Serial.println("ETH: WARNING - failed to set default route");
      }
#else
      {
        uint32_t eth_ip_u32 = (uint32_t)ETH.localIP();
        struct netif *nif;
        for (nif = netif_list; nif != NULL; nif = nif->next) {
          if (nif->ip_addr.u_addr.ip4.addr == eth_ip_u32) {
            netif_set_default(nif);
            Serial.println("ETH: Set as default route (preferred for MQTT)");
            break;
          }
        }
      }
#endif
      _ntp_start_pending = true;   // started from the main task by loop_ntp()
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
#endif // !CONFIG_WIFI_AP
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("WiFi STA: Got IP %s\n", WiFi.localIP().toString().c_str());
      _ntp_start_pending = true;   // started from the main task by loop_ntp()
      break;
    default:
      break;
  }
}

void testClient(const char * host, uint16_t port)
{
  Serial.print("\nconnecting to ");
  Serial.println(host);

  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    Serial.write(client.read());
  }

  Serial.println("closing connection\n");
  client.stop();
}


// Pressing the button will pull down the input (it has a 10k pullup resistor)
#define BUTTON_PRESSED()  (!digitalRead (34))

// Start WiFi STA using saved credentials from config/captive portal
// Returns true if WiFi.begin() was called (credentials exist)
bool setup_wifi_sta() {
  if (wifi_ssid[0] == '\0') {
    Serial.println("WiFi STA: No SSID configured, skipping");
    return false;
  }

  // If already connected (e.g., from captive portal), just log and return
  if (WiFi.isConnected()) {
    IPAddress ip = WiFi.localIP();
    Serial.printf("WiFi STA: Already connected to '%s' IP=%d.%d.%d.%d\n",
                  wifi_ssid, ip[0], ip[1], ip[2], ip[3]);
    return true;
  }

  Serial.printf("WiFi STA: Connecting to '%s'...\n", wifi_ssid);
  // Ensure STA is enabled without disrupting AP mode (portal) or ETH
  wifi_mode_t current_mode = WiFi.getMode();
  if ((current_mode & WIFI_STA) == 0) {
    // Add STA to current mode (preserve AP if portal is running)
    if (current_mode & WIFI_AP) {
      WiFi.mode(WIFI_AP_STA);
    } else {
      WiFi.mode(WIFI_STA);
    }
  }
  WiFi.begin(wifi_ssid, wifi_password);
  return true;
}

void setup_ethernet() {
  WiFi.onEvent(WiFiEvent);
#ifndef CONFIG_WIFI_AP
  ETH.begin();
#endif
}

#define MDNS_BROKER_POLL_MS 30000
static unsigned long mdns_last_poll = 0;

void loop_mdns() {
  if (!mdns_discovery) return;                                 // re-query is the querier's job (ESP32-6du.6)
  // No mDNS polling in configured-only mode
  if (mqtt_mode == MQTT_MODE_CONFIGURED_ONLY) return;

  // Stop searching once an mDNS broker is found
  if (mdns_has_broker()) return;

  // Stop searching if fallback broker is connected (discovery-with-fallback mode)
  if (mqtt_mode == MQTT_MODE_DISCOVERY_WITH_FALLBACK && mqtt_connected()) return;

  if (millis() - mdns_last_poll < MDNS_BROKER_POLL_MS) return;
  mdns_last_poll = millis();

  mdns_query_mqtt_broker();
}
