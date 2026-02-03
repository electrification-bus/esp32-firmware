#pragma once
#include <Arduino.h>

bool setup_wifi();
bool setup_mdns();
void setup_ota();
void setup_ethernet();

void loop_ota();

bool client_connected();

// Get IP addresses as strings (dot-separated format)
const char* get_wifi_ip();
const char* get_eth_ip();

// Get MAC addresses as strings (colon-separated format)
const char* get_eth_mac();
const char* get_wlan_mac();

// Get the mDNS hostname (sanitized device name)
const char* get_mdns_hostname();


