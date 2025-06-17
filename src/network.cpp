#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <network.h>
#include <WiFiUdp.h>

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

bool setup_mdns() {
  if (!MDNS.begin("esp32")) {
    Serial.println("Error setting up MDNS responder!");
    return false;
  }
  Serial.println("mDNS responder started");
  return true;
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


bool setup_ota() {
  
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
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
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