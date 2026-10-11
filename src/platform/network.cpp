#include <Arduino.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <lwip/netif.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#ifndef CONFIG_WIFI_AP
#include <ETH.h>
#endif
#ifdef EBUS_ETH_W5500
#include <SPI.h>
#endif
#include <time.h>
#include <platform/config.h>
#include <platform/network.h>
#include <ebus/discovery/broker_browse.h>
#include <ebus/discovery/mdns_strings.h>
#include <homie/homie.h>
#include <homie/homie_id.h>
#include <platform/log_stream.h>
#include <platform/mdns_backend_esp32.h>
#include <platform/mdns_services.h>

// Forward declaration to avoid circular include with mqtt_client.h
extern boolean mqtt_connected();

#define WIFI_SSID "foo"
#define WIFI_PW   "bar"

int CONNECT_ATTEMPTS = 6;
WiFiMulti wifiMulti;
#ifndef CONFIG_WIFI_AP
static bool eth_connected = false;
#endif

// Hostname for mDNS and DHCP: the sanitized device id (shared across functions)
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
#ifndef CONFIG_WIFI_AP
static char eth_ip_str[16] = {0};
#endif

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

// Get the mDNS hostname (sanitized device id)
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

// Convert a name to a valid mDNS hostname. A Homie id (lowercase letters, digits, single
// hyphens, no hyphen at either end) is already a valid DNS label, so the device id keeps
// every character it has and the hostname, the DHCP name and the MQTT topic are one string.
static void sanitize_hostname(const char* input, char* output, size_t max_len) {
  sanitize_homie_id(input, output, max_len);
}

// The mDNS stack behind the core's discovery code (ebus/discovery): advertising
// (mdns_register_services()), broker browsing and re-resolution.
static Esp32MdnsBackend mdns_backend;

// Forward declaration - registers all mDNS services
void mdns_register_services();

// mDNS-discovered MQTT broker (separate from config broker). The connection goes to the
// resolved IP address; the host name is kept so the address can be resolved again when
// the broker stops answering there (mdns_reresolve_broker()).
static BrokerFound mdns_broker = {};

#define MDNS_DISCOVERY_ATTEMPTS 3
#define MDNS_DISCOVERY_DELAY_MS 3000
#define MDNS_BROWSE_TIMEOUT_MS 3000   // ESPmDNS's queryService() waits this long itself
#define MDNS_RERESOLVE_TIMEOUT_MS 2000

// Discover an MQTT broker via mDNS, storing the result in mdns_broker. Browses the
// service types in mqtt_discover (config.yml mqtt.discover) in order; the default is
// _secure-mqtt._tcp (TLS, 8883), the form TLS-only brokers (e.g. SPAN) advertise, then
// _mqtt._tcp (plain, 1883), which the eBus spec only expects when TLS isn't feasible.
// The first result with a resolved address wins; the service's default port is used when
// the SRV record carries none. A failed search leaves an earlier result in place. Does
// not modify mqtt_host/mqtt_port globals.
static bool mdns_query_mqtt_broker() {
  char order[96] = {0};
  size_t pos = 0;
  for (uint8_t k = 0; k < mqtt_discover.count && pos < sizeof(order); k++) {
    pos += snprintf(order + pos, sizeof(order) - pos, "%s%s", k == 0 ? "" : ", then ",
                    broker_service_type(mqtt_discover.services[k]));
  }
  Serial.printf("mDNS: Searching for MQTT brokers (%s)...\n", order);

  for (int attempt = 0; attempt < MDNS_DISCOVERY_ATTEMPTS; attempt++) {
    if (attempt > 0) {
      Serial.printf("mDNS: Discovery attempt %d of %d...\n", attempt + 1, MDNS_DISCOVERY_ATTEMPTS);
      delay(MDNS_DISCOVERY_DELAY_MS);
    }

    BrokerFound found;
    if (!broker_browse(&mdns_backend, &mqtt_discover, MDNS_BROWSE_TIMEOUT_MS, &found)) continue;
    mdns_broker = found;
    if (mdns_broker.hostname[0] == '\0') {
      Serial.println("mDNS: Broker host name unusable, the address cannot be re-resolved");
    }
    Serial.printf("mDNS: Discovered %s broker via %s: %s:%u (host=%s)\n",
                   mdns_broker.port == MQTT_SECURE_PORT ? "MQTTS" : "MQTT",
                   broker_service_type(mdns_broker.service), mdns_broker.address,
                   mdns_broker.port, mdns_broker.hostname);
    return true;
  }

  Serial.println("mDNS: No MQTT broker found via mDNS");
  return false;
}

bool mdns_reresolve_broker() {
  if (mdns_broker.hostname[0] == '\0') {
    Serial.println("mDNS: Broker has no host name to re-resolve");
    return false;
  }
  Serial.printf("mDNS: Re-resolving broker %s.local (last at %s)\n",
                 mdns_broker.hostname, mdns_broker.address);
  char previous[sizeof(mdns_broker.address)];
  memcpy(previous, mdns_broker.address, sizeof(previous));
  switch (broker_reresolve(&mdns_backend, &mdns_broker, MDNS_RERESOLVE_TIMEOUT_MS)) {
    case BROKER_RERESOLVE_MOVED:
      Serial.printf("mDNS: %s.local moved from %s to %s\n",
                     mdns_broker.hostname, previous, mdns_broker.address);
      return true;
    case BROKER_RERESOLVE_UNCHANGED:
      Serial.printf("mDNS: %s.local is still at %s\n", mdns_broker.hostname, mdns_broker.address);
      return false;
    default:
      Serial.printf("mDNS: No answer for %s.local, keeping %s\n",
                     mdns_broker.hostname, mdns_broker.address);
      return false;
  }
}

const char* mdns_get_broker_host() {
  return mdns_broker.address;
}

uint16_t mdns_get_broker_port() {
  return mdns_broker.port;
}

bool mdns_has_broker() {
  return mdns_broker.address[0] != '\0';
}

// With mdns_enabled on, arduino-esp32 3.x always claims <device id>.local; the gates
// control only the querier and the service records. The 2.0.x responder has a crash bug
// (doc/MDNS.md), so on 2.0.x nothing starts unless a gate asks for it.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static const bool MDNS_HOSTNAME_ALWAYS = true;
#else
static const bool MDNS_HOSTNAME_ALWAYS = false;
#endif

bool setup_mdns() {
  // The hostname is the resolved device id, so one image gives every board its own name.
  char base_hostname[48] = {0};
  sanitize_hostname(getDeviceID(), base_hostname, sizeof(base_hostname));

  // mdns_enabled=false: nothing at all. The DHCP hostname (setup_ethernet()) is unaffected.
  if (!mdns_enabled) {
    Serial.printf("mDNS: disabled by config; not claiming %s.local\n", base_hostname);
    return false;
  }

  // Two independent gates: mdns_discovery (querier: find the broker) and mdns_advertise
  // (responder: register service records). Defaults are build-conditional; see the #if
  // in config.cpp.
  if (!MDNS_HOSTNAME_ALWAYS && !mdns_discovery && !mdns_advertise) {
    Serial.println("mDNS: disabled (mdns_discovery=false, mdns_advertise=false)");
    return false;
  }

  strncpy(mdns_hostname, base_hostname, sizeof(mdns_hostname) - 1);

  // The Arduino MDNS API couples mdns_init() with a hostname claim, so MDNS.begin()
  // is required even for the querier. A claimed hostname is defended against a true
  // collision by probing and appending a suffix. A 2.0.x querier-only start does a
  // single begin(), keeping the responder surface minimal (no service/TXT records).
  if (MDNS_HOSTNAME_ALWAYS || mdns_advertise) {
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

  // Responder: advertise our service records (the mDNS responder crash surface).
  if (mdns_advertise) {
    mdns_register_services();
  }

  return true;
}

// Primary MAC as lowercase hex without colons: Ethernet on Ethernet builds, WiFi otherwise.
static const char* get_primary_mac() {
  static char mac_str[13] = {0};
  if (mac_str[0] == '\0') {
    uint8_t mac[6];
#ifndef CONFIG_WIFI_AP
    esp_read_mac(mac, ESP_MAC_ETH);
#else
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  return mac_str;
}

// Framework versions from their build-time macros, e.g. "arduino-esp32 3.3.11 / ESP-IDF 5.5.5".
static const char* get_os_version() {
  static char os_str[48] = {0};
  if (os_str[0] == '\0') {
    snprintf(os_str, sizeof(os_str), "arduino-esp32 %d.%d.%d / ESP-IDF %d.%d.%d",
             ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH,
             ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
  }
  return os_str;
}

// Register all mDNS services - called from setup_mdns() when mdns_advertise is on.
// The TXT records come from the core's builders (ebus/discovery/txt_records.h), which
// follow framework.md "Detail: mDNS Discovery", plus the homie_* compatibility keys on
// _ebus._tcp; doc/MDNS.md maps each one.
void mdns_register_services() {
  MdnsServicesConfig cfg = {};
  cfg.device_id = getDeviceID();
  cfg.homie_role = homie_role;
  cfg.manufacturer = device_manufacturer;
  cfg.model = device_model;
  cfg.serial = device_serial;
  cfg.device_type = device_type;
  cfg.name = device_name;
  cfg.fw_version = firmware_version;
  cfg.os_version = get_os_version();
  cfg.mac = get_primary_mac();
  cfg.http_port = 80;
  cfg.ebus_port = mqtt_port;
  // _telnet._tcp, the serial log stream, so a monitor can find the device by name
  // instead of by IP. Only advertised when the listener is actually up.
  cfg.log_port = log_tcp_running() ? log_tcp_get_port() : 0;

  MdnsServicesResult result;
  mdns_services_advertise(&mdns_backend, &cfg, &result);
  EbusIdentity id;
  mdns_services_identity(&cfg, &id);

  for (uint8_t i = 0; i < MDNS_SERVICE_COUNT; i++) {
    const char* type = mdns_service_type(i);
    switch (result.status[i]) {
      case MDNS_SERVICE_ADVERTISED:
        if (i == MDNS_SERVICE_HTTP) {
          Serial.printf("mDNS: Added %s service (path=%s)\n", type, MDNS_VAL_HTTP_PATH);
        } else if (i == MDNS_SERVICE_LOG) {
          Serial.printf("mDNS: Added %s service (serial log, port %u)\n", type, cfg.log_port);
        } else if (i == MDNS_SERVICE_EBUS) {
          Serial.printf("mDNS: Added %s service (roles=%s)\n", type, ebus_roles_string(id.roles));
        } else {
          Serial.printf("mDNS: Added %s service\n", type);
        }
        break;
      case MDNS_SERVICE_BUILD_FAILED:
        Serial.printf("mDNS: **ERROR -- %s TXT record not built (TxtResult %u); not advertised\n",
                      type, (unsigned)result.txt[i]);
        break;
      case MDNS_SERVICE_REFUSED:
        Serial.printf("mDNS: **ERROR -- the mDNS stack refused %s\n", type);
        break;
      default:
        break;
    }
  }
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
      ETH.setHostname(mdns_hostname);   // set from the device id in setup_ethernet()
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

// Start WiFi STA using saved credentials from config/captive portal.
// Returns true if WiFi.begin() was called (credentials exist and STA is enabled).
// wifi_sta_enabled (config.yml wifi.enabled) gates it once a configuration has loaded.
// Recovery mode (no configuration) joins NVS-saved WiFi whatever the build default, so a
// board whose only working uplink is WiFi can still be reached and re-flashed.
bool setup_wifi_sta() {
  if (wifi_ssid[0] == '\0') {
    Serial.println("WiFi STA: No SSID configured, skipping");
    return false;
  }
  if (!wifi_sta_enabled) {
    if (config_loaded) {
      Serial.printf("WiFi STA: off (wifi.enabled=false); not joining '%s'\n", wifi_ssid);
      return false;
    }
    Serial.println("WiFi STA: recovery mode, joining the NVS-saved network although wifi.enabled is off");
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
  // DHCP hostname on both interfaces: the device id, the same name mDNS claims. Set
  // before ETH.begin() and before any WiFi.mode() call so both netifs pick it up.
  sanitize_hostname(getDeviceID(), mdns_hostname, sizeof(mdns_hostname));
  WiFi.setHostname(mdns_hostname);
  WiFi.onEvent(WiFiEvent);
#ifdef EBUS_ETH_W5500
  // SPI Ethernet (boards without an EMAC, such as the ESP32-S3); ETH.begin() resets the
  // W5500 through its RST pin.
  SPI.begin(EBUS_ETH_SPI_SCK, EBUS_ETH_SPI_MISO, EBUS_ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_W5500, 1, EBUS_ETH_SPI_CS, EBUS_ETH_SPI_INT, EBUS_ETH_SPI_RST, SPI);
#elif !defined(CONFIG_WIFI_AP)
  // ESP.restart() (OTA, /reboot) is a CPU reset that leaves the Ethernet MAC as it was,
  // and about one boot in four after one never gets a working link. A deep-sleep wake
  // resets the whole digital domain, so turn a software restart into one before ETH.begin().
  if (esp_reset_reason() == ESP_RST_SW) {
    Serial.println("ETH: software restart; resetting through a 10 ms deep sleep");
    Serial.flush();
    esp_sleep_enable_timer_wakeup(10000);
    esp_deep_sleep_start();
  }
#ifdef EBUS_ETH_PHY_TYPE
  // The board variant's pins with a different PHY, e.g. ETH_PHY_LAN8720 on a wESP32
  // older than the RTL8201 revision the framework's wesp32 variant assumes.
  ETH.begin(EBUS_ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
#else
  ETH.begin();
#endif
#endif
}

#define MDNS_BROKER_POLL_MS 30000
static unsigned long mdns_last_poll = 0;

void loop_mdns() {
  if (!mdns_discovery) return;                                 // re-query is the querier's job
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
