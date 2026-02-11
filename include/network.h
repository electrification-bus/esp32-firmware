#pragma once
#include <Arduino.h>

bool setup_wifi();
bool setup_mdns();
void setup_ota();
void setup_ethernet();

void loop_ota();
void loop_mdns();

bool client_connected();

// Get IP addresses as strings (dot-separated format)
const char* get_wifi_ip();
const char* get_eth_ip();

// Get MAC addresses as strings (colon-separated format)
const char* get_eth_mac();
const char* get_wlan_mac();

// Get the mDNS hostname (sanitized device name)
const char* get_mdns_hostname();

// mDNS-discovered MQTT broker (discovered independently from config)
// setup_mdns() performs initial discovery; results are available via accessors
bool mdns_has_broker();
const char* mdns_get_broker_host();
uint16_t mdns_get_broker_port();
