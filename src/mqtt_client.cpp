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

subscribed_settable_property_t settable_properties[128];

// Fallback callback for unhandled messages (used by controller)
static mqtt_fallback_callback_t _fallback_callback = nullptr;

void mqtt_set_fallback_callback(mqtt_fallback_callback_t callback) {
    _fallback_callback = callback;
}

// Reconnect callback (to re-publish state after reconnection)
static mqtt_reconnect_callback_t _reconnect_callback = nullptr;

void mqtt_set_reconnect_callback(mqtt_reconnect_callback_t callback) {
    _reconnect_callback = callback;
}

// Function prototype for mqtt_publish_json
void mqtt_publish_json(const char* subtopic, const JsonDocument* payload);

void generateTopics() {
  sprintf(topic_device,"%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID());
  sprintf(topic_cmd,"%s/%s",topic_device, HOMIE_TOPIC_SET);
}

void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance) {
   // Check if topic already registered (avoid duplicates on reconnection)
   for(int i=0; i<num_subscribers; i++) {
     if (strcmp(settable_properties[i].topic, topic) == 0) {
       // Already registered, update callback/instance in case they changed
       settable_properties[i].property_callback = cb;
       settable_properties[i].instance = instance;
       return;
     }
   }
   // New subscription
   strcpy(settable_properties[num_subscribers].topic, topic);
   settable_properties[num_subscribers].property_callback = cb;
   settable_properties[num_subscribers].instance = instance;
   num_subscribers++;
}


void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity) {
   for(int i=0;i<num_subscribers;i++) {
     if (strcmp(settable_properties[i].topic, topic) == 0) {
       //add entity pointer
       Serial.printf("ENTITY: Callback Subscription: %s SUCCESS\n", topic);
       settable_properties[i].entity_callback = cb;
       settable_properties[i].entity = entity;
       return;
     }
   }
   Serial.printf("ENTITY: Callback Subscription:FAIL - %s NOT FOUND\n", topic);
}

boolean mqtt_connect() {
  Serial.printf("MQTT Connecting: '%s': timeout in:%d\r\n", mqtt_host, transportClient.getTimeout());
  if (transportClient.connect(mqtt_host, mqtt_port) != 1) //8883 for TLS
  {
     Serial.println("MQTT connect timeout.");
     // todo ENABLE_DEBUG_MQTT=0;
     return (0);
  }

  //transportClient.setTimeout(60);//(MQTT_TIMEOUT);
  mqtt_client.setSocketTimeout(15);  // 15 seconds socket timeout
  mqtt_client.setBufferSize(MAX_DATA_LEN + 200);
  mqtt_client.setKeepAlive(90);      // 90 seconds keep-alive
  mqtt_client.setCallback(subscriber_callback);

  //construct will
  char lwt[64] = {0};
  sprintf(lwt,"%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(),HOMIE_$STATE);
  Serial.printf("MQTT will topic:'%s' msg:'%s'\n", lwt, HOMIE_STATE_LOST);
  if (strlen(mqtt_username) == 0) {
    //allows for anonymous connection
    mqtt_client.connect(getDeviceID(), lwt, 1, 1, HOMIE_STATE_LOST);
  } else {
    mqtt_client.connect(getDeviceID(), mqtt_username, mqtt_password,  lwt, 1, 1, HOMIE_STATE_LOST); // Attempt to connect
  }

  if (mqtt_client.state() == 0) {
    Serial.printf("MQTT connected: %s\r\n", mqtt_host);
    //re-subscribe to topics if needed
    for(int i=0;i<num_subscribers;i++) {
      Serial.printf("MQTT subscribing to topic: %s\n", settable_properties[i].topic);
      mqtt_client.subscribe(settable_properties[i].topic);
    }
    // Call reconnect callback to re-publish state (overrides LWT "lost")
    if (_reconnect_callback) {
      _reconnect_callback();
    }
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
    char topicBuf[256];
    char buf[256];
    Serial.printf("MQTT publish: size:%d chars", strlen(data));
    do {
      int pos = strcspn(data, ":");
      strncpy(buf, data, pos);
      buf[pos] = 0;
      snprintf(topicBuf, sizeof(topicBuf), "%s%s/%s", topic_device, subtopic, buf);
      //topic_ptr[pos] = 0;
      data += pos;
      if (*data++ == 0) {
        break;
      }

      pos = strcspn(data, ",");
      strncpy(mqtt_data, data, pos);
      mqtt_data[pos] = 0;
      data += pos;

      if (!mqtt_client.publish(topicBuf, mqtt_data, true)) {
       Serial.println("MQTT publish: failed");
      }
#ifdef ENABLE_DEBUG_MQTT
      Serial.printf("topic: %s, data: %s\n", topicBuf, mqtt_data);
#endif
    } while (*data++ != 0);
}

// MQTT Subscriber callback
//
// all subscriptions arrive here
// Note: Homie $description payloads can be large, use static buffer to avoid stack overflow
static char _mqtt_payload_buf[MAX_DATA_LEN];

void subscriber_callback(char* topic, uint8_t* payload, unsigned int length) {
  //sanity
  if (length > MAX_DATA_LEN - 1) {
    Serial.printf("MQTT CALLBACK: not handled: payload len overrun:%d (max:%d)\n", length, MAX_DATA_LEN - 1);
    return;
  }

  char* payload_buf = _mqtt_payload_buf;
  strncpy(payload_buf, (char*)payload, length);
  payload_buf[length] = '\0'; //ensure null-termination
  Serial.printf("\n***MQTT CALLBACK: topic '%s', payload '%s'\n", topic, payload_buf);

  //route to subscriber property
  bool handled = false;
  for(int i=0;i<num_subscribers;i++) {
    if (strcmp(settable_properties[i].topic, topic) == 0) {
      handled = true;
      if (settable_properties[i].property_callback && settable_properties[i].instance) {
        Serial.printf("MQTT: Invoking property callback for topic '%s'\n", topic);
          (settable_properties[i].instance->*settable_properties[i].property_callback)(topic, payload_buf);
      }
      if (settable_properties[i].entity_callback && settable_properties[i].entity) {
          Serial.printf("MQTT: Invoking entity callback for topic '%s'\n", topic);
          (settable_properties[i].entity->*settable_properties[i].entity_callback)(settable_properties[i].instance);
      }
    }
  }

  // If no property handler matched, forward to fallback callback (e.g., controller)
  // Pass the already null-terminated payload_buf to avoid duplicate buffer allocation
  if (!handled && _fallback_callback) {
    _fallback_callback(topic, (uint8_t*)payload_buf, length);
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
    } else if (millis() - last_connect_error > MQTTPublish_rootrate) { //throttle connection retries
      last_connect_error = millis();
      Serial.printf("MQTT not connected - retrying: %s\n", mqtt_host);
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