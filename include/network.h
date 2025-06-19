#pragma once
#include <Arduino.h>

bool setup_wifi();
bool setup_mdns();
void setup_ota();
void setup_ethernet();

void loop_ota();

bool wifi_client_connected();
const char* get_wifi_ip();


