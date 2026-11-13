#pragma once
#include <Arduino.h>

bool setup_wifi();
bool setup_wifi_sta();  // Start WiFi STA using saved wifi_ssid/wifi_password credentials
bool setup_mdns();
void setup_ethernet();

// NB: no setup_ota()/loop_ota() here. ArduinoOTA (espota, port 3232) was removed —
// it was never called, and its protocol has the device dial back to a TCP port on
// the *host*, which does not survive NAT/VPN/host firewalls. Push images instead to
// POST /api/v1/ota/{firmware,filesystem} on the portal (see captive_portal.cpp); pulling
// from a URL still works via ota_start_update() in platform/ota.h.
void loop_mdns();

// Start SNTP once an interface has a lease (main task only; see network.cpp).
void loop_ntp();
// Call before anything that resolves host (MQTT connect, OTA pull, the OA3 VEN's GET),
// from any task: makes the cache-clearing first lookup after an address change safely
// (see network.cpp). False when host does not resolve.
bool network_dns_prime(const char* host);

bool client_connected();

// Get IP addresses as strings (dot-separated format)
const char* get_wifi_ip();
const char* get_eth_ip();

// Get MAC addresses as strings (colon-separated format)
const char* get_eth_mac();
const char* get_wlan_mac();

// Get the mDNS hostname (sanitized device id)
const char* get_mdns_hostname();

// mDNS-discovered MQTT broker (discovered independently from config)
// setup_mdns() performs initial discovery; results are available via accessors
bool mdns_has_broker();
const char* mdns_get_broker_host();
uint16_t mdns_get_broker_port();
// Resolve the discovered broker's .local host name again (one mDNS query, no browsing).
// Returns true when it answered from a new address, which mdns_get_broker_host() then
// returns; false when it did not answer, answered from the same address, or no broker
// host name is known.
bool mdns_reresolve_broker();
