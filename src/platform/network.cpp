#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <esp_mac.h>
#include <lwip/netif.h>
#ifndef CONFIG_WIFI_AP
#include <ETH.h>
#endif
#include <time.h>
#include <platform/config.h>
#include <platform/network.h>
#include <homie/homie.h>
#include <platform/mdns_strings.h>

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
    const char* hostname = MDNS.hostname(i).c_str();
    uint16_t svc_port = MDNS.port(i);
    Serial.printf("mDNS:   [%d] host=%s port=%u\n", i, hostname, svc_port);

    // Log TXT records for debugging
    int txt_count = MDNS.numTxt(i);
    for (int j = 0; j < txt_count; j++) {
      Serial.printf("mDNS:     TXT %s=%s\n",
                     MDNS.txtKey(i, j).c_str(), MDNS.txt(i, j).c_str());
    }

    // Use the broker's IP address directly
    IPAddress broker_ip = MDNS.IP(i);
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
  // service records, the ESP32-9mx crash surface). Both off by default.
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
  mdns_add_txt(MDNS_SVC_DEVICE_INFO, MDNS_PROTO_TCP, MDNS_TXT_MANUFACTURER, device_manufacturer);
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


// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

void setup_ota() {
  
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      const char* type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.print("Start updating ");
      Serial.println(type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop_ota() {
  ArduinoOTA.handle();
}


/******************** Ethernet and SD card ****************** */
/*
    This sketch is combined demo of: 1) SD(esp_32) -- > SD_Test; 2) WiFi --> ETH_LAN8720
    It displays info about the card on the terminal and initializes LAN after that. You can ping the IP address shown on the terminal.
*/

#include "FS.h"
//#include "SD_MMC.h"

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
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      setenv("TZ", NTP_TIMEZONE, 1);
      tzset();
      Serial.printf("NTP: sync started, TZ=%s\n", NTP_TIMEZONE);
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
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      setenv("TZ", NTP_TIMEZONE, 1);
      tzset();
      Serial.printf("NTP: sync started, TZ=%s\n", NTP_TIMEZONE);
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
