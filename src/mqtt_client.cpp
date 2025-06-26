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
#include <homie/Property.h>
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
int num_subscribers = 0;

struct Subscribed_properties {
    char topic[64];
    subscriber_callback_t callback;
    Property* instance; // store the instance to call the member function on
};
Subscribed_properties subscribed_properties[128];

// Function prototype for mqtt_publish_json
void mqtt_publish_json(const char* subtopic, const JsonDocument* payload);

void generateTopics() {
  sprintf(topic_device,"%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID());
  sprintf(topic_cmd,"%s/%s",topic_device, HOMIE_TOPIC_SET);
}

void subscribe_for_callbacks(const char* topic, subscriber_callback_t cb, Property* instance) {
   //Serial.printf("Callback Subscription #%d: %s\n", num_subscribers, topic);
   strcpy(subscribed_properties[num_subscribers].topic, topic);
   subscribed_properties[num_subscribers].callback = cb;
   subscribed_properties[num_subscribers].instance = instance;
   num_subscribers++;
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
  mqtt_client.setCallback(subscriber_callback);
    
  //construct will
  char lwt[64] = {0};
  sprintf(lwt,"%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(),HOMIE_$STATE);
  Serial.printf("MQTT will topic:'%s' msg:'%s'\n", lwt, HOMIE_STATE_LOST);
  if (mqtt_username.isEmpty()) {
    //allows for anonymous connection
    mqtt_client.connect(getDeviceID(), lwt, 1, 1, HOMIE_STATE_LOST); 
  } else {
    mqtt_client.connect(getDeviceID(), mqtt_username.c_str(), mqtt_password.c_str(),  lwt, 1, 1, HOMIE_STATE_LOST); // Attempt to connect
  }

  if (mqtt_client.state() == 0) {
    Serial.printf("MQTT connected: %s\r\n", mqtt_host.c_str());
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
    sprintf(topicBuf, "%s/%s", topic_device, subtopic);
    if (!mqtt_client.publish(topicBuf, jsonString, true)) {
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

      if (!mqtt_client.publish(topicBuf.c_str(), mqtt_data, true)) {
       Serial.println("MQTT publish: failed");
      }
#ifdef ENABLE_DEBUG_MQTT
      Serial.printf("topic: %s, data: %s\n", topicBuf.c_str(), mqtt_data);
#endif
    } while (*data++ != 0);
}

// MQTT Subscriber callback
//
// all subscriptions arrive here
void subscriber_callback(char* topic, uint8_t* payload, unsigned int length) {
  //sanity
  if (length > 254) {
    Serial.printf("MQTT CALLBACK: not handled: payload len overrun:%d\n", length);
    return;
  }

  char payload_buf[length+1] = {0};
  strncpy(payload_buf, (char*)payload, length);
  payload_buf[length] = '\0'; //ensure null-termination
  Serial.printf("\n***MQTT CALLBACK: topic '%s', payload '%s'\n", topic, payload_buf);

  //route to subscriber property
  for(int i=0;i<num_subscribers;i++) {
    if (strcmp(subscribed_properties[i].topic, topic) == 0) {
      if (subscribed_properties[i].callback && subscribed_properties[i].instance) {
          (subscribed_properties[i].instance->*subscribed_properties[i].callback)(topic, payload_buf);
      }
    }
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