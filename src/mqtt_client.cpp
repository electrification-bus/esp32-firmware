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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#define ENABLE_DEBUG_MQTT = 1

//WiFiClientSecure transportClient; TODO
WiFiClient transportClient;
MQTTClient mqtt_client(MAX_DATA_LEN + 200);  // Buffer size for MQTT packets
#define TOPIC_TOKEN_SIZ 64
#define MQTT_DATA_SIZ 128
unsigned long mqtt_interval_ts = 0;
static char mqtt_data[MQTT_DATA_SIZ] = "";
static int mqtt_connection_error_count = 0;
char topic_device[TOPIC_TOKEN_SIZ] = {0};
char topic_cmd[TOPIC_TOKEN_SIZ] = {0};
// Static LWT topic buffer - must persist after mqtt_connect() returns
// (pointer corruption fix per arduino-mqtt issue #89)
static char lwt_topic[64] = {0};
int num_subscribers = 0;

// ============================================================================
// MQTT Publish Queue - for thread-safe publishing from other tasks/cores
// ============================================================================
#define MQTT_QUEUE_SIZE 16
#define MQTT_TOPIC_MAX 128
#define MQTT_PAYLOAD_MAX 64

struct MqttQueueItem {
    char topic[MQTT_TOPIC_MAX];
    char payload[MQTT_PAYLOAD_MAX];
    bool retained;
};

static QueueHandle_t mqtt_publish_queue = nullptr;

// Initialize the MQTT publish queue (call from setup)
void mqtt_queue_init() {
    if (mqtt_publish_queue == nullptr) {
        mqtt_publish_queue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(MqttQueueItem));
        if (mqtt_publish_queue != nullptr) {
            Serial.println("MQTT: Publish queue created");
        } else {
            Serial.println("MQTT: ERROR - Failed to create publish queue");
        }
    }
}

// Queue a publish request (thread-safe, can be called from any task/core)
bool mqtt_queue_publish(const char* topic, const char* payload, bool retained) {
    if (mqtt_publish_queue == nullptr) {
        Serial.println("MQTT: Queue not initialized, publishing directly");
        return mqtt_client.publish(topic, payload, retained, 0);
    }

    MqttQueueItem item;
    strncpy(item.topic, topic, MQTT_TOPIC_MAX - 1);
    item.topic[MQTT_TOPIC_MAX - 1] = '\0';
    strncpy(item.payload, payload, MQTT_PAYLOAD_MAX - 1);
    item.payload[MQTT_PAYLOAD_MAX - 1] = '\0';
    item.retained = retained;

    // Non-blocking queue send (don't wait if queue is full)
    if (xQueueSend(mqtt_publish_queue, &item, 0) != pdTRUE) {
        Serial.println("MQTT: Queue full, message dropped");
        return false;
    }
    return true;
}

// Process queued publish requests (call from main loop on Core 0)
void mqtt_process_queue() {
    if (mqtt_publish_queue == nullptr) return;

    MqttQueueItem item;
    // Process up to 4 messages per loop iteration to avoid blocking
    int processed = 0;
    while (processed < 4 && xQueueReceive(mqtt_publish_queue, &item, 0) == pdTRUE) {
        if (mqtt_client.connected()) {
            mqtt_client.publish(item.topic, item.payload, item.retained, 0);
        }
        processed++;
    }
}

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
  Serial.printf("MQTT Connecting: '%s:%d'\r\n", mqtt_host, mqtt_port);

  // Configure WiFiClient socket timeout before using it
  transportClient.setTimeout(10);  // 10 seconds socket timeout

  // setOptions(keepAlive, cleanSession, timeout)
  // keepAlive: 5 seconds - very short to ensure frequent pings (per arduino-mqtt issue #177)
  // cleanSession: false - maintain session across reconnects
  // timeout: 1000ms - short timeout for operations
  mqtt_client.setOptions(5, false, 1000);
  Serial.printf("MQTT: configured keepalive=%ds, timeout=%dms\n", 5, 1000);

  // Set Last Will and Testament (LWT) - published by broker if connection lost
  // Use static buffer lwt_topic to prevent pointer corruption (arduino-mqtt issue #89)
  // Use QoS 0 - QoS 1 causes connection issues with this library
  sprintf(lwt_topic, "%s/%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID(), HOMIE_$STATE);
  mqtt_client.setWill(lwt_topic, HOMIE_STATE_LOST, true, 0);  // retained=true, qos=0
  Serial.printf("MQTT will topic:'%s' msg:'%s'\n", lwt_topic, HOMIE_STATE_LOST);


   // Configure MQTT client BEFORE connecting
  // begin() sets up the network client and host
  mqtt_client.begin(mqtt_host, mqtt_port, transportClient);

  
  // Connect with or without credentials
  bool connected = false;
  if (strlen(mqtt_username) == 0) {
    // Anonymous connection
    connected = mqtt_client.connect(getDeviceID());
  } else {
    connected = mqtt_client.connect(getDeviceID(), mqtt_username, mqtt_password);
  }

  if (connected) {
    Serial.printf("MQTT connected: %s\r\n", mqtt_host);
    // Re-subscribe to topics if needed
    for (int i = 0; i < num_subscribers; i++) {
      Serial.printf("MQTT subscribing to topic: %s\n", settable_properties[i].topic);
      mqtt_client.subscribe(settable_properties[i].topic);
    }
    // Call reconnect callback to re-publish state (overrides LWT "lost")
    if (_reconnect_callback) {
      _reconnect_callback();
    }
  } else {
    Serial.printf("MQTT failed: error=%d, returnCode=%d\n",
                  mqtt_client.lastError(), mqtt_client.returnCode());
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
    if (!mqtt_client.publish(topicBuf, jsonString, true, 0)) {  // retained=true, qos=0
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

      if (!mqtt_client.publish(topicBuf, mqtt_data, true, 0)) {  // retained=true, qos=0
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

// MQTTClient uses onMessageAdvanced callback with signature:
// void callback(MQTTClient *client, char topic[], char bytes[], int length)
void subscriber_callback(MQTTClient* client, char topic[], char bytes[], int length) {
  (void)client;  // unused

  //sanity
  if (length > MAX_DATA_LEN - 1) {
    Serial.printf("MQTT CALLBACK: not handled: payload len overrun:%d (max:%d)\n", length, MAX_DATA_LEN - 1);
    return;
  }

  char* payload_buf = _mqtt_payload_buf;
  strncpy(payload_buf, bytes, length);
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
  mqtt_client.onMessageAdvanced(subscriber_callback);
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

unsigned long last_connect_error = 0;
static unsigned long last_loop_success = 0;

static unsigned long last_connected_log = 0;
static bool was_connected = false;

void loop_mqtt() {
    // Must call loop() frequently to handle keepalive pings and receive messages
    // MQTTClient::loop() returns false if connection was lost
    bool loop_ok = mqtt_client.loop();

    bool is_connected = mqtt_client.connected();

    if (is_connected && loop_ok) {
        // Connection is healthy
        last_loop_success = millis();

        // Log connection status periodically (every 30 seconds)
        if (millis() - last_connected_log > 30000) {
            last_connected_log = millis();
            unsigned long uptime = millis() / 1000;
            Serial.printf("MQTT: connected OK (uptime: %lus) %s/%s\n", uptime, mqtt_host, topic_device);
        }
        was_connected = true;
    } else {
        // Connection lost or loop failed
        if (was_connected) {
            // Just disconnected - log immediately
            Serial.printf("MQTT: DISCONNECTED! error=%d, loop_ok=%d, uptime=%lus\n",
                          mqtt_client.lastError(), loop_ok, millis() / 1000);
            was_connected = false;
        }

        if (millis() - last_connect_error > MQTTPublish_rootrate) {
            last_connect_error = millis();
            Serial.printf("MQTT not connected (error=%d) - retrying: %s\n",
                          mqtt_client.lastError(), mqtt_host);
            mqtt_connect();
        }
    }
    
    // Process queued publish requests from other tasks (thread-safe)
    // Do this AFTER loop() to prioritize keepalive pings
    mqtt_process_queue();
    mqtt_interval_ts = millis();
}

void mqtt_restart() {
  if (mqtt_client.connected()) {
    mqtt_client.disconnect();
  }
}

boolean mqtt_connected() {
  return mqtt_client.connected();
}