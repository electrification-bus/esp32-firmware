/*
   -------------------------------------------------------------------
   EmonESP Serial to Emoncms gateway
   -------------------------------------------------------------------
   Adaptation of Chris Howells OpenEVSE ESP Wifi
   by Trystan Lea, Glyn Hudson, OpenEnergyMonitor

   Modified to use with the CircuitSetup.us Split Phase Energy Meter by jdeglavina
   Modified to use with EMS Workshop by dmendonca

   All adaptation GNU General Public License as below.

   -------------------------------------------------------------------

   This file is part of OpenEnergyMonitor.org project.
   EmonESP is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.
   EmonESP is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with EmonESP; see the file COPYING.  If not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include "mqtt_client.h"
#include <TimeLib.h>
#include <WiFiClientSecure.h>
#include <ArduinoYaml.h>
#include <config.h>
#define ENABLE_DEBUG_MQTT = 1

//WiFiClientSecure transportClient; TODO
WiFiClient transportClient;
PubSubClient mqtt_client(transportClient);
#define TOPIC_TOKEN_SIZ 64
#define MQTT_DATA_SIZ 128
unsigned long mqtt_interval_ts = 0;
static char mqtt_data[MQTT_DATA_SIZ] = "";
static int mqtt_connection_error_count = 0;
char topic_device[TOPIC_TOKEN_SIZ] = {0};
char topic_cmd[TOPIC_TOKEN_SIZ] = {0};

// Function prototype for mqtt_publish_json
void mqtt_publish_json(const char* subtopic, const JsonDocument* payload);

void generateTopics() {
  sprintf(topic_device,"%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID());
  sprintf(topic_cmd,"%s/%s",topic_device, HOMIE_TOPIC_SET);
}

boolean mqtt_connect() {
  Serial.printf("MQTT Connecting: '%s': timeout in:%d\r\n", mqtt_host.c_str(), transportClient.getTimeout());
  if (transportClient.connect(mqtt_host.c_str(), mqtt_port) != 1) //8883 for TLS
  {
     Serial.println("MQTT connect timeout.");
     // todo ENABLE_DEBUG_MQTT=0;
     return (0);
  }

  //transportClient.setTimeout(60);//(MQTT_TIMEOUT);
  mqtt_client.setSocketTimeout(6);//MQTT_TIMEOUT);
  mqtt_client.setBufferSize(MAX_DATA_LEN + 200);
  mqtt_client.setKeepAlive(180);

  if (mqtt_username.isEmpty()) {
    //allows for anonymous connection
    // TODO: will, qos, retain
    // boolean connect(const char* id, const char* willTopic, uint8_t willQos, boolean willRetain, const char* willMessage);
    mqtt_client.connect(getDeviceID()); // Attempt to connect
  } else {
    mqtt_client.connect(getDeviceID(), mqtt_username.c_str(), mqtt_password.c_str()); // Attempt to connect
  }

  if (mqtt_client.state() == 0) {
    Serial.printf("MQTT connected: %s\r\n", mqtt_host.c_str());
    
    //subscribe to command topic
    if (!mqtt_client.subscribe(topic_cmd)) {
      delay(250);
      if (!mqtt_client.subscribe(topic_cmd)) {
        delay(500);
        if (!mqtt_client.subscribe(topic_cmd)) {
          Serial.printf("MQTT: FAILED TO SUBSCRIBE TO COMMAND TOPIC: %s\r\n", topic_cmd);
          return false;
        }
      }
    }
    Serial.printf("MQTT: SUBSCRIBED TO COMMAND TOPIC: %s\r\n", topic_cmd);
    //  mqttclient.publish(getDeviceTopic().c_str(), "connected"); // Once connected, publish an announcement..
  } else {
    Serial.println("MQTT failed: ");
    Serial.println(mqtt_client.state());
    return (0);
  }
  return (1);
}

void mqtt_publish_json(const char* subtopic, const JsonDocument * payload) {

    if (measureJson(*payload) >= 1024) {
      Serial.println("MQTT publish: payload too large");
      return;
    }
    char topicBuf[256];
    char jsonString[1024];
    serializeJson(*payload, jsonString);
    // It's annoying to have to set this limit, but maybe a static size is better for performance?
    //char data[1024];
    //jsonString.toCharArray(data, sizeof(data));
    //topicBuf.concat(subtopic);
    sprintf(topicBuf, "%s/%s", topic_device, subtopic);
    //if (!mqttclient.publish(topicBuf.c_str(), data)) {
    if (!mqtt_client.publish(topicBuf, jsonString)) {
        Serial.println("MQTT publish: failed");
    }
    Serial.printf("topic: %s, data: %s\n", topicBuf, jsonString);
}

//pull apart a comma-sep colon-delim name:value string and publish the name:value pairs under 'subtopic'
void mqtt_publish_comma_sep_colon_delim(const char* subtopic, const char * data) {
    String topicBuf;
    char buf[256];
    Serial.printf("MQTT publish: size:%d chars", strlen(data));
    do {
      int pos = strcspn(data, ":");
      strncpy(buf, data, pos);
      buf[pos] = 0;
      String st(subtopic);
      topicBuf = topic_device;
      topicBuf.concat(st+"/");
      topicBuf.concat(buf);
      //topic_ptr[pos] = 0;
      data += pos;
      if (*data++ == 0) {
        break;
      }

      pos = strcspn(data, ",");
      strncpy(mqtt_data, data, pos);
      mqtt_data[pos] = 0;
      data += pos;

      if (!mqtt_client.publish(topicBuf.c_str(), mqtt_data)) {
       Serial.println("MQTT publish: failed");
      }
#ifdef ENABLE_DEBUG_MQTT
      Serial.printf("topic: %s, data: %s\n", topicBuf.c_str(), mqtt_data);
#endif
    } while (*data++ != 0);
}

// Subscriber callback
//
// We're subscribed to the following topics:
// tbd
//
void subscriber_callback(char* topic, uint8_t* payload, unsigned int length) {
  //sanity
  if (length > 254) {
    Serial.printf("MQTT CALLBACK: not handled: payload len overrun:%d\n", length);
    return;
  }
  if (strcmp(topic, topic_cmd) == 0) {
    char payload_buf[length+1] = {0};
    strncpy(payload_buf, (char*)payload, length);
    payload_buf[length] = '\0'; //ensure null-termination
    Serial.printf("\n***MQTT CALLBACK: topic '%s', payload '%s'\n", topic, payload_buf);
    if (strstr(payload_buf, "report") == 0) {
      //trigger a data-model dump
      return;
    }
    // add new commands here
  }
}

void setup_mqtt() {
  generateTopics();
  mqtt_client.setCallback(subscriber_callback);
  if (!mqtt_connect()) {
    delay(250);
    if (!mqtt_connect()) {
      delay(500);
      if (!mqtt_connect()) {
        Serial.println("MQTT: FAILED TO CONNECT");
        return;
      }
    }
  }
  mqtt_interval_ts = now();
}

int last_connect_error = millis();
void loop_mqtt() {
    if (mqtt_client.connected()) {  
      //publish here; call publishers 
    } else if (millis() - last_connect_error > 10000) { //throttle connection retries
      last_connect_error = millis();
      Serial.printf("MQTT not connected - retrying: %s\n", mqtt_host.c_str());
      mqtt_connect();
    }
    mqtt_interval_ts = millis();
    mqtt_client.loop();
}

void mqtt_restart() {
  if (mqtt_client.connected()) {
    mqtt_client.disconnect();
  }
}

boolean mqtt_connected() {
  return mqtt_client.connected();
}