#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <esp_mac.h>
#include <ETH.h>
#include <config.h>
#include <network.h>
#include <homie/homie.h>

#define WIFI_SSID "foo"
#define WIFI_PW   "bar"

int CONNECT_ATTEMPTS = 6;
WiFiMulti wifiMulti;
static bool eth_connected = false;

// Static buffer for sanitized hostname (shared across functions)
static char mdns_hostname[64] = {0};

bool client_connected() {
  return eth_connected || (WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA)));
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
  if (eth_connected) {
    IPAddress ip = ETH.localIP();
    snprintf(eth_ip_str, sizeof(eth_ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return eth_ip_str;
  }
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
    sprintf(eth_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  return eth_mac_str;
}

const char* get_wlan_mac() {
  if (wlan_mac_str[0] == '\0') {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(wlan_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
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

bool setup_mdns() {
  // Use device name as hostname (sanitized for mDNS)
  if (strlen(device_name) > 0) {
    sanitize_hostname(device_name, mdns_hostname, sizeof(mdns_hostname));
  } else {
    // Fallback to device ID if no name set
    strncpy(mdns_hostname, getDeviceID(), sizeof(mdns_hostname) - 1);
  }

  if (!MDNS.begin(mdns_hostname)) {
    Serial.println("mDNS: Error starting responder!");
    return false;
  }
  Serial.printf("mDNS: Started for host: %s.local\n", mdns_hostname);

  // ============================================================
  // _homie._tcp service - Homie protocol device/controller discovery
  // This is the primary service for Homie-compliant discovery
  // Advertises: hostname.local with Homie device attributes
  // ============================================================
  MDNS.addService("_homie", "_tcp", mqtt_port);

  // Homie protocol attributes (required and recommended)
  mdns_add_txt("_homie", "_tcp", "homie", "5.0");  // Homie version without patch
  mdns_add_txt("_homie", "_tcp", "version", "1");  // Description document version
  mdns_add_txt("_homie", "_tcp", "id", getDeviceID());
  mdns_add_txt("_homie", "_tcp", "name", device_name);
  mdns_add_txt("_homie", "_tcp", "role", homie_role);  // "device" or "controller"

  if (strlen(device_type) > 0) {
    mdns_add_txt("_homie", "_tcp", "type", device_type);
  }

  // Build and publish base topic: homie/5/{device_id}/
  char base_topic[96] = {0};
  snprintf(base_topic, sizeof(base_topic), "%s/%s/", HOMIE_TOPIC_PREFIX, getDeviceID());
  mdns_add_txt("_homie", "_tcp", "base", base_topic);

  // MQTT broker discovery info - allows clients to find the MQTT broker
  mdns_add_txt("_homie", "_tcp", "mqtt_host", mqtt_host);
  char port_str[8] = {0};
  snprintf(port_str, sizeof(port_str), "%u", mqtt_port);
  mdns_add_txt("_homie", "_tcp", "mqtt_port", port_str);
  mdns_add_txt("_homie", "_tcp", "mqtt_tls", mqtt_is_secure() ? "true" : "false");

  // Device state (will be "init" at mDNS setup time)
  mdns_add_txt("_homie", "_tcp", "state", device_state_to_cstr(theDevice.state()));

  // Implementation info
  mdns_add_txt("_homie", "_tcp", "implementation", "homie-esp32-sdk");

  Serial.printf("mDNS: Added _homie._tcp service (role=%s, base=%s)\n", homie_role, base_topic);

  // ============================================================
  // _mqtt._tcp service - MQTT broker advertisement
  // Allows discovery of the MQTT broker this device uses
  // Standard service type for MQTT (RFC 6763)
  // ============================================================
  MDNS.addService("_mqtt", "_tcp", mqtt_port);
  mdns_add_txt("_mqtt", "_tcp", "host", mqtt_host);
  mdns_add_txt("_mqtt", "_tcp", "port", port_str);
  mdns_add_txt("_mqtt", "_tcp", "tls", mqtt_is_secure() ? "true" : "false");
  if (strlen(mqtt_username) > 0) {
    mdns_add_txt("_mqtt", "_tcp", "auth", "true");
  } else {
    mdns_add_txt("_mqtt", "_tcp", "auth", "false");
  }

  Serial.printf("mDNS: Added _mqtt._tcp service (host=%s, port=%u)\n", mqtt_host, mqtt_port);

  // ============================================================
  // _device-info._tcp service - Hardware/firmware information
  // Standard service type for device information
  // ============================================================
  MDNS.addService("_device-info", "_tcp", 80);

  // Manufacturer and model
  mdns_add_txt("_device-info", "_tcp", "manufacturer", device_manufacturer);
  mdns_add_txt("_device-info", "_tcp", "model", device_model);

  // Serial number (use device ID if not set)
  if (strlen(device_serial) > 0) {
    mdns_add_txt("_device-info", "_tcp", "serial_number", device_serial);
  } else {
    mdns_add_txt("_device-info", "_tcp", "serial_number", getDeviceID());
  }

  // MAC addresses
  mdns_add_txt("_device-info", "_tcp", "eth_mac", get_eth_mac());
  mdns_add_txt("_device-info", "_tcp", "wlan_mac", get_wlan_mac());

  // Firmware info
  if (strlen(firmware_name) > 0) {
    mdns_add_txt("_device-info", "_tcp", "fw_name", firmware_name);
  }
  mdns_add_txt("_device-info", "_tcp", "fw_version", firmware_version);

  // Platform info
  mdns_add_txt("_device-info", "_tcp", "platform", "ESP32");
  mdns_add_txt("_device-info", "_tcp", "sdk_version", ESP.getSdkVersion());

  // Network addresses (if available)
  const char* eth_ip = get_eth_ip();
  if (strlen(eth_ip) > 0) {
    mdns_add_txt("_device-info", "_tcp", "eth_ip", eth_ip);
  }
  const char* wifi_ip = get_wifi_ip();
  if (strlen(wifi_ip) > 0) {
    mdns_add_txt("_device-info", "_tcp", "wlan_ip", wifi_ip);
  }

  Serial.printf("mDNS: Added _device-info._tcp service\n");
  Serial.printf("mDNS: ETH MAC: %s, WLAN MAC: %s\n", get_eth_mac(), get_wlan_mac());

  return true;
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
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // Set eth hostname to match mDNS hostname
      // Use the same sanitized hostname that mDNS uses
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
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
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

void setup_ethernet()
{

  WiFi.onEvent(WiFiEvent);
  ETH.begin();
}


void loop_ethernet() {
}
