#include <Arduino.h>
#include <WiFiMulti.h>
#include <wifi.h>

#define WIFI_SSID "REDACTED_WIFI_SSID"
#define WIFI_PW "REDACTED_WIFI_PASSWORD"

int CONNECT_ATTEMPTS = 6;
WiFiMulti wifiMulti;

bool wifi_client_connected() {
  return WiFi.isConnected() && (WIFI_STA == (WiFi.getMode() & WIFI_STA));
}

// Function to get the IP address as a string
String get_wifi_ip() {
  if (wifi_client_connected()) {
    return WiFi.localIP().toString();
  }
  return "Not Connected";
}

bool setup_wifi() {
  Serial.printf("wifi connecting: %s\n", WIFI_SSID);
  wifiMulti.addAP(WIFI_SSID, WIFI_PW);
  while (wifiMulti.run() != WL_CONNECTED && (CONNECT_ATTEMPTS-- > 0)) {
    delay(1000);
    Serial.printf("wifi failed to connect - retrying %s\n", WIFI_SSID);
  }
  bool didConnect = wifi_client_connected();
  Serial.printf("WiFi: %s: %s\n", WIFI_SSID, didConnect ?  WiFi.localIP().toString().c_str() : "FAILED TO CONNECT");
  return didConnect;
}