// SPDX-License-Identifier: MIT
#include <platform/mqtt_client.h>
#include <homie.h>
#include <TimeLib.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <platform/config.h>
#include <platform/network.h>
#include <homie/Property.h>
#include <node/NodeEntity.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Fetching the broker's root CA needs a TLS handshake (~50 KB of heap). The POE-ISO
// has no PSRAM; on arduino-esp32 2.0.x that was too tight, on 3.x it leaves room.
#if !defined(ESP32_POE_ISO) || ESP_ARDUINO_VERSION_MAJOR >= 3
#define MQTT_FETCH_ROOT_CA 1
#endif
#include <LittleFS.h>
#include <mbedtls/base64.h>

// Transport clients - use secure or insecure based on port
WiFiClient insecureClient;
WiFiClientSecure secureClient;
Client* transportClient = nullptr;  // Pointer to active client
bool mqtt_using_secure = false;

// Client packet buffer. An inbound PUBLISH must fit whole in it: 1 byte packet type,
// up to 4 bytes remaining length, 2 bytes topic length, the topic (every topic the
// firmware uses, nested child $description included, is under 128 bytes), 2 bytes
// packet id at QoS 1/2, then a payload of up to MAX_DATA_LEN bytes. That is
// MAX_DATA_LEN + 137; 256 bytes of headroom covers it with margin.
#define MQTT_PACKET_HEADROOM 256
MQTTClient mqtt_client(MAX_DATA_LEN + MQTT_PACKET_HEADROOM);
MqttClientTransport mqtt_transport(mqtt_client);

// Set once begin() has bound the client to a transport. Until then loop_mqtt() does not
// call loop() and mqtt_connected() does not call connected().
static bool _mqtt_begun = false;

char topic_device[HOMIE_DEVICE_TOPIC_MAX + 1] = {0};
// Static LWT topic buffer - must persist after broker_connect() returns
// (pointer corruption fix per arduino-mqtt issue #89)
static char lwt_topic[HOMIE_TOPIC_MAX + 1] = {0};
int num_subscribers = 0;

// ============================================================================
// MQTT Publish Queue - for thread-safe publishing from other tasks/cores
// ============================================================================
#define MQTT_QUEUE_SIZE 16
#define MQTT_TOPIC_MAX (HOMIE_TOPIC_MAX + 1)   // buffer size, NUL included
// Sized to match Property::_value, so a value that fits in the property model also fits
// through the queue. It was 64, which silently truncated anything longer — a JSON array
// of cell voltages, an enum list, a long string — and drivers worked around it by
// bypassing the queue entirely (see examples/bms, which published cell_voltages with a
// direct mqtt_client.publish for exactly this reason).
#define MQTT_PAYLOAD_MAX 256

// What a queued item asks for once it is drained, which is the point: both kinds of work
// have to happen OUTSIDE the MQTT receive callback, and this is the one place that runs
// after mqtt_client.loop() has returned.
enum MqttQueueKind : uint8_t {
    MQTT_QUEUE_PUBLISH = 0,   // send `payload` to `topic`
    MQTT_QUEUE_SET,           // dispatch `payload` as a /set on `topic`
};

struct MqttQueueItem {
    char topic[MQTT_TOPIC_MAX];
    char payload[MQTT_PAYLOAD_MAX];
    int  length;        // explicit byte count — carries 0x00 (empty-string value, C3)
    bool retained;
    Property* source;   // told when this is sent or dropped (publish-on-change memo), or null
    MqttQueueKind kind = MQTT_QUEUE_PUBLISH;   // every producer sets it; see the note below
};

// ── Why an inbound /set is queued here ──────────────────────────────────────────────
// A /set must not be ACTED ON from inside subscriber_callback(). Two reasons:
//
//  - Dispatch publishes. Property::publish_set() sends $target and the value, and a
//    retained publish is QoS 2 by default (C4), so the client waits for its PUBREC/
//    PUBCOMP by pumping its own receive loop — which delivers the next message into the
//    same handler, one frame deeper. The controller's subscribe path went down exactly
//    that way (26b9dc4): six frames before a large $description copy overflowed the stack.
//  - Drivers actuate relays, Modbus and BLE. Running that inside the receive callback
//    holds the client mid-packet for as long as the hardware takes.
//
// The DISPATCH is deferred, not its publishes. Queueing the publishes instead would leave
// them droppable when the queue is full, and the settable contract
// (doc/WritingADriver.md) makes dispatch the thing that COVERS for a dropped publish —
// it names a full queue as the case where "dispatch publishes the value itself". Running
// the dispatch out here keeps its publishes direct, so nothing on the /set path can be
// dropped once it is queued. Drivers already publish through this queue
// (NodeProperty::publish -> Property::publish_queued), so they need no change.
//
// A /set occupies one slot and enqueues nothing, so it cannot crowd out telemetry; a
// message arriving while the queue drains can only add another item, so dispatch never
// nests.

static QueueHandle_t mqtt_publish_queue = nullptr;

// Defined below, next to the settable table it walks; mqtt_process_queue() runs it.
static bool dispatch_settable(const char* topic, const char* value);

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

// Queue a publish request (thread-safe, can be called from any task/core).
// Null-terminated convenience overload: length is strlen(payload).
bool mqtt_queue_publish(const char* topic, const char* payload, bool retained) {
    return mqtt_queue_publish(topic, payload, (int)strlen(payload), retained);
}

// Length-aware variant: publishes exactly `length` bytes, so a single 0x00 byte
// (the Homie empty-string value, C3) survives instead of being treated as a
// null-terminated zero-length payload. memcpy (not strncpy) preserves embedded NULs.
bool mqtt_queue_publish(const char* topic, const char* payload, int length, bool retained,
                        Property* source) {
    if (length < 0) length = 0;
    if (length > MQTT_PAYLOAD_MAX) {
        // Truncating a payload silently produces a subtly wrong retained value that looks
        // plausible — worse than a dropped message, because nothing downstream can tell.
        // Say so, and say by how much, so the fix (raise MQTT_PAYLOAD_MAX) is obvious.
        Serial.printf("MQTT: **ERROR -- payload for '%s' is %d bytes, max %d — TRUNCATED "
                      "(raise MQTT_PAYLOAD_MAX)\n", topic, length, MQTT_PAYLOAD_MAX);
        length = MQTT_PAYLOAD_MAX;
    }
    if (mqtt_publish_queue == nullptr) {
        Serial.println("MQTT: Queue not initialized, publishing directly");
        bool ok = mqtt_client.publish(topic, payload, length, retained, homie_qos(retained));
        if (source) source->queued_publish_done(payload, length, ok);
        return ok;
    }

    size_t topic_len = strlen(topic);
    if (topic_len >= MQTT_TOPIC_MAX) {
        // A truncated topic would publish to some other topic, so refuse. Logged once: a
        // driver publishing on a timer would otherwise repeat it every period.
        static bool reported = false;
        if (!reported) {
            reported = true;
            Serial.printf("MQTT: **ERROR -- topic is %u chars, over %d; publish refused: %s\n",
                          (unsigned)topic_len, MQTT_TOPIC_MAX - 1, topic);
        }
        if (source) source->queued_publish_done(payload, length, false);
        return false;
    }

    MqttQueueItem item;
    memcpy(item.topic, topic, topic_len + 1);
    memcpy(item.payload, payload, length);
    item.length = length;
    item.retained = retained;
    item.source = source;
    item.kind = MQTT_QUEUE_PUBLISH;

    // Non-blocking queue send (don't wait if queue is full)
    if (xQueueSend(mqtt_publish_queue, &item, 0) != pdTRUE) {
        Serial.println("MQTT: Queue full, message dropped");
        if (source) source->queued_publish_done(payload, length, false);
        return false;
    }
    return true;
}

// Process queued publish requests (call from main loop on Core 0)
void mqtt_process_queue() {
    if (mqtt_publish_queue == nullptr) return;

    MqttQueueItem item;
    // Process up to 4 items per loop iteration to avoid blocking
    int processed = 0;
    while (processed < 4 && xQueueReceive(mqtt_publish_queue, &item, 0) == pdTRUE) {
        processed++;
        if (item.kind == MQTT_QUEUE_SET) {
            // Runs the full settable contract — validate, store, driver, publish — here
            // rather than in the receive callback. dispatch_settable() publishes directly,
            // which is safe and required; see the note on MqttQueueKind.
            // NUL-terminated into a local because the queue carries an explicit length
            // and a 256-byte payload fills item.payload with no room for a terminator.
            char value[MQTT_PAYLOAD_MAX + 1];
            memcpy(value, item.payload, item.length);
            value[item.length] = '\0';
            if (!dispatch_settable(item.topic, value)) {
                // The property was registered when the message arrived, so losing it in
                // between means it was torn down — worth saying, not worth retrying.
                Serial.printf("MQTT: queued /set '%s' no longer has a settable property\n",
                              item.topic);
            }
            continue;
        }
        bool ok = false;
        if (mqtt_client.connected()) {
            // Length-aware publish so an empty-string value (single 0x00 byte) is sent
            // intact rather than collapsing to a zero-length (retract) payload (C3).
            // Retained -> configured QoS (default 2), non-retained -> QoS 0 (C4).
            ok = mqtt_client.publish(item.topic, item.payload, item.length, item.retained, homie_qos(item.retained));
        }
        if (item.source) item.source->queued_publish_done(item.payload, item.length, ok);
    }
}

// Heap-allocated on first subscription (not a static array: see Device.cpp's
// _description_json comment). settable_properties[i] indexing works unchanged
// since a pointer subscripts identically to an array.
//
// Sized to the device rather than to a constant. settable_table_capacity() is emitted
// into src/homie.cpp by ./ebus-esp32 generate (every settable property in device.yml,
// children included, plus the $state watch and a margin). A hand-coded src/homie.cpp
// may define it too; one that does not gets the weak default below.
// -DMAX_SETTABLE_SUBSCRIBERS=<n> in build_src_flags overrides both, for entries the
// generator cannot see.
#define SETTABLE_TABLE_DEFAULT 32
__attribute__((weak)) uint16_t settable_table_capacity() { return SETTABLE_TABLE_DEFAULT; }

static int settable_capacity = 0;
static subscribed_settable_property_t* settable_properties = nullptr;
static subscribed_settable_property_t* ensure_settable_properties() {
  if (settable_properties == nullptr) {
#ifdef MAX_SETTABLE_SUBSCRIBERS
    settable_capacity = MAX_SETTABLE_SUBSCRIBERS;
#else
    settable_capacity = settable_table_capacity();
#endif
    // (): value-initialise. Belt-and-braces with the struct's member initializers.
    settable_properties = new subscribed_settable_property_t[settable_capacity]();
    Serial.printf("MQTT: settable subscriber table: %d entries (%u bytes)\n", settable_capacity,
                  (unsigned)(settable_capacity * sizeof(subscribed_settable_property_t)));
  }
  return settable_properties;
}

// Every registration path refuses a topic once the table is full; this says how to fix it.
static void report_settable_table_full(const char* topic) {
  Serial.printf("MQTT: **ERROR -- settable subscriber table full (%d); /set for '%s' NOT "
                "registered. ./ebus-esp32 generate sizes it from the settable properties in "
                "device.yml; for entries added at runtime, build with "
                "-DMAX_SETTABLE_SUBSCRIBERS=<n> in build_src_flags\n", settable_capacity, topic);
}

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

#ifdef MQTT_FETCH_ROOT_CA
// Fetch root CA cert from broker via TLS on port 443 and save to SPIFFS
// Connects insecurely, extracts cert chain, finds root, saves as PEM.
// The PEM is encoded one 64-character line at a time into a stack buffer, so a
// root of any size needs no large scratch buffer.
static bool mqtt_fetch_and_save_root_cert() {
  char cert_path[96];
  mqtt_build_cert_path(mqtt_host, cert_path, sizeof(cert_path));

  Serial.printf("TLS: Fetching root CA from %s:%u\n", mqtt_host, mqtt_port);
  Serial.printf("TLS: Free heap before probe: %u bytes\n", (unsigned)ESP.getFreeHeap());

  // Reuse global secureClient for the probe to avoid allocating a second
  // TLS context (~40KB heap). We set it insecure for the probe, then
  // reconfigure it with the real CA cert afterward in mqtt_init_transport().
  secureClient.stop();  // Ensure clean state (free any stale socket/TLS resources)
  delay(100);           // Let LWIP release socket resources
  secureClient.setInsecure();
  secureClient.setTimeout(15);

  // Connect on the MQTTS port directly - same cert chain, guaranteed open
  ntp_lookup_wait();   // connect() resolves mqtt_host; see network.cpp
  Serial.printf("TLS: Connecting insecurely to %s:%u...\n", mqtt_host, mqtt_port);
  if (!secureClient.connect(mqtt_host, mqtt_port)) {
    Serial.printf("TLS: ERROR - Failed to connect to %s:%u\n", mqtt_host, mqtt_port);
    secureClient.stop();
    return false;
  }
  Serial.println("TLS: TLS handshake complete");

  // Get the peer certificate chain from the underlying mbedTLS context
  const mbedtls_x509_crt* peer_cert = secureClient.getPeerCertificate();
  if (peer_cert == nullptr) {
    Serial.println("TLS: ERROR - No peer certificate available");
    secureClient.stop();
    return false;
  }

  // Count certs in chain and find the root (last cert)
  int chain_len = 0;
  const mbedtls_x509_crt* cert = peer_cert;
  const mbedtls_x509_crt* root_cert = nullptr;

  while (cert != nullptr) {
    char cn_buf[64] = {0};
    mbedtls_x509_dn_gets(cn_buf, sizeof(cn_buf), &cert->subject);
    Serial.printf("TLS: [%d] %s\n", chain_len, cn_buf);

    root_cert = cert;  // Last cert in chain is the root
    cert = cert->next;
    chain_len++;
  }

  Serial.printf("TLS: Certificate chain has %d certificate(s)\n", chain_len);

  if (root_cert == nullptr) {
    Serial.println("TLS: ERROR - Could not identify root certificate");
    secureClient.stop();
    return false;
  }

  char root_cn[64] = {0};
  mbedtls_x509_dn_gets(root_cn, sizeof(root_cn), &root_cert->subject);
  Serial.printf("TLS: Root CA: %s\n", root_cn);

  size_t der_len = root_cert->raw.len;
  Serial.printf("TLS: Root cert DER size: %d bytes\n", der_len);

  // root_cert lives in secureClient's TLS context, so the PEM is written before the
  // connection is stopped.
  if (!LittleFS.begin(true)) {
    Serial.println("TLS: ERROR - Failed to mount SPIFFS to save cert");
    secureClient.stop();
    return false;
  }

  File certFile = LittleFS.open(cert_path, FILE_WRITE);
  if (!certFile) {
    Serial.printf("TLS: ERROR - Failed to create cert file '%s'\n", cert_path);
    secureClient.stop();
    return false;
  }

  certFile.print("-----BEGIN CERTIFICATE-----\n");

  // 48 DER bytes encode to one 64-character PEM line.
  unsigned char line[65];
  for (size_t i = 0; i < der_len; i += 48) {
    size_t chunk = (der_len - i < 48) ? (der_len - i) : 48;
    size_t line_len = 0;
    mbedtls_base64_encode(line, sizeof(line), &line_len, root_cert->raw.p + i, chunk);
    certFile.write(line, line_len);
    certFile.print("\n");
  }

  certFile.print("-----END CERTIFICATE-----\n");

  size_t file_size = certFile.position();   // size() reads 0 until the file is closed
  certFile.close();

  // Done with TLS probe - disconnect and free TLS resources
  secureClient.stop();

  Serial.printf("TLS: Root CA saved (%u bytes) to '%s'\n", (unsigned)file_size, cert_path);
  return true;
}
#endif // MQTT_FETCH_ROOT_CA

// Initialize transport client (secure or insecure based on port)
bool mqtt_init_transport() {
  mqtt_using_secure = mqtt_is_secure();

  if (mqtt_using_secure) {
    Serial.println("MQTT: Configuring TLS/SSL connection (port 8883)");

    // Load CA certificate from SPIFFS
    const char* caCert = mqtt_load_ca_cert();
#ifdef MQTT_FETCH_ROOT_CA
    // Auto-fetch the root CA cert if not cached
    if (caCert == nullptr) {
      Serial.println("TLS: No cached cert found, auto-fetching from broker...");
      if (mqtt_fetch_and_save_root_cert()) {
        caCert = mqtt_load_ca_cert();
      }
    }
#endif

    if (caCert != nullptr) {
      // Full TLS with CA verification
      secureClient.setCACert(caCert);
      Serial.println("MQTT: TLS transport configured with CA certificate");
      secureClient.setTimeout(15);  // 15 seconds for TLS handshake
      transportClient = &secureClient;
    } else {
#ifndef MQTT_FETCH_ROOT_CA
      // No root-CA fetch on this build; fall back to plain MQTT
      Serial.println("TLS: WARNING - No CA cert found in /certs/ SPIFFS folder");
      Serial.println("TLS: To fix: place cert in data/certs/ and upload filesystem");
      Serial.println("TLS: Falling back to insecure MQTT on port 1883");
      mqtt_port = 1883;
      mqtt_using_secure = false;
      transportClient = &insecureClient;
#else
      // Other boards: cert fetch failed, connect without verification
      Serial.println("TLS: WARNING - No CA cert available, connecting without verification");
      Serial.println("TLS: To fix: place cert in data/certs/ and upload filesystem");
      secureClient.setInsecure();
      secureClient.setTimeout(15);
      transportClient = &secureClient;
#endif
    }
  } else {
    Serial.printf("MQTT: Using insecure connection (port %d)\n", mqtt_port);
    transportClient = &insecureClient;
  }
  return true;
}

void generateTopics() {
  snprintf(topic_device, sizeof(topic_device), "%s/%s", HOMIE_TOPIC_PREFIX, getDeviceID());
}

// A table entry holds the topic in a fixed char[]; refuse one that does not fit rather
// than truncate it (a truncated key never matches an inbound /set).
static bool settable_topic_fits(const char* topic) {
  size_t len = strlen(topic);
  if (len < sizeof(settable_properties[0].topic)) return true;
  Serial.printf("MQTT: /set topic is %u bytes, over the %u-byte limit; not registered: %s\n",
                (unsigned)len, (unsigned)(sizeof(settable_properties[0].topic) - 1), topic);
  return false;
}

void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance) {
   ensure_settable_properties();
   if (!settable_topic_fits(topic)) return;
   // Check if topic already registered (avoid duplicates on reconnection)
   for(int i=0; i<num_subscribers; i++) {
     if (strcmp(settable_properties[i].topic, topic) == 0) {
       // Already registered, update callback/instance in case they changed
       settable_properties[i].property_callback = cb;
       settable_properties[i].instance = instance;
       return;
     }
   }
   if (num_subscribers >= settable_capacity) {
     report_settable_table_full(topic);
     return;
   }
   // New subscription. Null the entity fields explicitly: an entry created here may
   // never be visited by entity_subscribe_for_callbacks(), and dispatch branches on them.
   strlcpy(settable_properties[num_subscribers].topic, topic, sizeof(settable_properties[0].topic));
   settable_properties[num_subscribers].property_callback = cb;
   settable_properties[num_subscribers].instance = instance;
   settable_properties[num_subscribers].entity_callback = nullptr;
   settable_properties[num_subscribers].entity = nullptr;
   num_subscribers++;
}

// D3: register a per-property fn-ptr handler (+context) for a /set topic. Updates an
// existing entry's handler in place (reconnect-safe), else appends a new subscriber.
void subscribe_settable_handler(const char* topic, settable_handler_t handler, void* ctx) {
   ensure_settable_properties();
   if (!settable_topic_fits(topic)) return;
   for (int i = 0; i < num_subscribers; i++) {
     if (strcmp(settable_properties[i].topic, topic) == 0) {
       settable_properties[i].handler = handler;
       settable_properties[i].handler_ctx = ctx;
       return;
     }
   }
   if (num_subscribers >= settable_capacity) {
     report_settable_table_full(topic);
     return;
   }
   // Null the property/entity fields explicitly. A topic registered ONLY here — a
   // NodeProperty::on_set() handler, or a watch on a device attribute like $state — has
   // no Property or NodeEntity behind it, and dispatch tests both before calling.
   strlcpy(settable_properties[num_subscribers].topic, topic, sizeof(settable_properties[0].topic));
   settable_properties[num_subscribers].handler = handler;
   settable_properties[num_subscribers].handler_ctx = ctx;
   settable_properties[num_subscribers].property_callback = nullptr;
   settable_properties[num_subscribers].instance = nullptr;
   settable_properties[num_subscribers].entity_callback = nullptr;
   settable_properties[num_subscribers].entity = nullptr;
   num_subscribers++;
}


void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity) {
   ensure_settable_properties();
   if (!settable_topic_fits(topic)) return;
   for(int i=0;i<num_subscribers;i++) {
     if (strcmp(settable_properties[i].topic, topic) == 0) {
       //add entity pointer
       Serial.printf("ENTITY: Callback Subscription: %s SUCCESS\n", topic);
       settable_properties[i].entity_callback = cb;
       settable_properties[i].entity = entity;
       return;
     }
   }
   // Topic not found — create entry (Property::subscribe may not have run yet)
   if (num_subscribers < settable_capacity) {
     Serial.printf("ENTITY: Callback Subscription: %s CREATED NEW\n", topic);
     strlcpy(settable_properties[num_subscribers].topic, topic, sizeof(settable_properties[0].topic));
     settable_properties[num_subscribers].property_callback = nullptr;
     settable_properties[num_subscribers].instance = nullptr;
     settable_properties[num_subscribers].entity_callback = cb;
     settable_properties[num_subscribers].entity = entity;
     num_subscribers++;
     if (mqtt_transport.connected()) {
       mqtt_transport.subscribe(topic, 0);
     }
   } else {
     report_settable_table_full(topic);
   }
}

// Call the driver behind one subscriber: the D3 fn-ptr handler, then the entity callback
// (Property* overload when a Homie Property exists, else the name-based one with the
// property id parsed back out of the topic). Returns false if a driver refused the value,
// true if every driver present accepted it or there is none. A handler that refuses stops
// the chain: the entity callback is not called.
static bool call_settable_driver(subscribed_settable_property_t* s, const char* topic,
                                 const char* value) {
  bool accepted = true;
  if (s->handler) {
      Serial.printf("MQTT: Invoking settable handler for topic '%s'\n", topic);
      accepted = s->handler(s->handler_ctx, value);
  }
  if (accepted && s->entity_callback && s->entity) {
      if (s->instance) {
          // Homie Property exists — use Property* overload
          Serial.printf("MQTT: Invoking entity callback for topic '%s'\n", topic);
          accepted = (s->entity->*s->entity_callback)(s->instance);
      } else {
          // NodeProperty settable — extract property ID from topic, call name-based overload
          const char* set_slash = strrchr(topic, '/');  // /set
          if (set_slash && set_slash > topic) {
              const char* p = set_slash - 1;
              while (p > topic && *p != '/') p--;
              if (*p == '/') p++;
              char pid[HOMIE_PROPERTY_ID_MAX + 1];
              int plen = set_slash - p;
              if (plen > HOMIE_PROPERTY_ID_MAX) plen = HOMIE_PROPERTY_ID_MAX;
              strncpy(pid, p, plen);
              pid[plen] = '\0';
              Serial.printf("MQTT: Invoking entity callback '%s'='%s'\n", pid, value);
              accepted = s->entity->entity_settable_callback(pid, value);
          }
      }
  }
  return accepted;
}

// Deliver `value` to every settable subscriber registered for `topic`. For one with a
// Homie Property: validate the payload against its datatype and format (an invalid one is
// logged and goes no further), store it so the driver can read property->value(), and
// call the driver. Only if the driver accepts are the value and $target published (the
// value only if the driver did not queue a publish of it already); if it refuses, the
// property's previous value is put back and nothing is published.
// Returns true if at least one subscriber matched.
//
// Factored out of subscriber_callback() so deliver_local_set() takes the IDENTICAL route
// as an inbound message. One dispatch path, so a driver never needs to care where a set
// came from. Both must run on the main task (the MQTT loop runs in loop(); see
// deliver_local_set() in mqtt_client.h), which is what lets the snapshot go without a lock.
static bool dispatch_settable(const char* topic, const char* value) {
  bool handled = false;
  for(int i=0;i<num_subscribers;i++) {
    subscribed_settable_property_t* s = &settable_properties[i];
    if (strcmp(s->topic, topic) != 0) continue;
    handled = true;
    Property* prop = s->property_callback ? s->instance : nullptr;
    Property::ValueSnapshot before;
    uint32_t queued_before = 0;
    if (prop) {
      prop->save_value(&before);
      if (!(prop->*s->property_callback)(value)) continue;   // invalid: logged, not stored
      queued_before = prop->queued_count();
    }
    bool accepted = call_settable_driver(s, topic, value);
    if (!prop) continue;
    if (accepted) {
      prop->publish_set(value, prop->queued_count() != queued_before);
    } else {
      prop->restore_value(&before);
      Serial.printf("MQTT: '%s' = '%s' refused by its driver, not published\n", topic, value);
    }
  }
  return handled;
}

// Is a settable property registered for this topic? subscriber_callback() defers the
// dispatch but must still decide NOW whether the message is ours or the fallback's,
// because the payload buffer is only valid until it returns.
static bool is_settable_topic(const char* topic) {
  for (int i = 0; i < num_subscribers; i++) {
    if (strcmp(settable_properties[i].topic, topic) == 0) return true;
  }
  return false;
}

// Record an inbound /set on the publish queue for mqtt_process_queue() to dispatch.
// Called from the receive callback, so it only copies: no publish, no driver, no client
// call. A refusal here is logged and the command is dropped — dispatching it from the
// callback is the very thing the queue exists to avoid.
static bool mqtt_queue_set(const char* topic, const char* payload, int length) {
  if (strlen(topic) >= MQTT_TOPIC_MAX) {
    Serial.printf("MQTT: /set topic is %u chars, over the %d queued; refused: %s\n",
                  (unsigned)strlen(topic), MQTT_TOPIC_MAX - 1, topic);
    return false;
  }
  if (length < 0 || length > MQTT_PAYLOAD_MAX) {
    // store_set_payload() would refuse it anyway rather than store half a value, and
    // queueing a truncated payload would hand a plausible-looking wrong value to the
    // hardware. Say so instead.
    Serial.printf("MQTT: /set payload for '%s' is %d bytes, over the %d a property holds; "
                  "refused\n", topic, length, MQTT_PAYLOAD_MAX);
    return false;
  }
  if (mqtt_publish_queue == nullptr) {
    Serial.printf("MQTT: queue not initialized, /set '%s' refused\n", topic);
    return false;
  }

  MqttQueueItem item;
  memcpy(item.topic, topic, strlen(topic) + 1);
  memcpy(item.payload, payload, (size_t)length);
  item.length = length;
  item.retained = false;      // unused for a set
  item.source = nullptr;      // no publish-on-change memo to report back to
  item.kind = MQTT_QUEUE_SET;

  if (xQueueSend(mqtt_publish_queue, &item, 0) != pdTRUE) {
    Serial.printf("MQTT: queue full, /set '%s' = '%s' DROPPED\n", topic, payload);
    return false;
  }
  return true;
}

// Set a property from INSIDE the firmware — no broker, no network, no loopback publish.
// Anything that needs to actuate another node by topic uses this, and it keeps working
// with the broker unreachable. The property's own publish() (called by the driver's
// setter) is what reports the resulting value back to MQTT, exactly as it would for a
// remote /set.
//
// `topic` is the full /set topic, e.g. "homie/5/<device-id>/relay1/state/set".
// Returns false if no settable property is registered for it — the caller should treat
// that as a configuration error (bad node/property id), not a transient failure.
bool deliver_local_set(const char* topic, const char* value) {
  Serial.printf("MQTT: local set '%s' = '%s'\n", topic, value);
  bool handled = dispatch_settable(topic, value);
  if (!handled) {
    Serial.printf("MQTT: local set NOT DELIVERED — no settable property registered for '%s'\n", topic);
  }
  return handled;
}

// MQTT Subscriber callback
//
// all subscriptions arrive here
// Note: Homie $description payloads can be large, use a heap buffer (allocated on
// first use, not a static array — see Device.cpp's _description_json comment) to
// avoid stack overflow.
static char* _mqtt_payload_buf = nullptr;

// MQTTClient uses onMessageAdvanced callback with signature:
// void callback(MQTTClient *client, char topic[], char bytes[], int length)
void subscriber_callback(MQTTClient* client, char topic[], char bytes[], int length) {
  (void)client;  // unused

  //sanity
  if (length > MAX_DATA_LEN - 1) {
    Serial.printf("MQTT CALLBACK: not handled: payload len overrun:%d (max:%d)\n", length, MAX_DATA_LEN - 1);
    return;
  }

  if (_mqtt_payload_buf == nullptr) {
    _mqtt_payload_buf = new char[MAX_DATA_LEN];
  }
  char* payload_buf = _mqtt_payload_buf;
  strncpy(payload_buf, bytes, length);
  payload_buf[length] = '\0'; //ensure null-termination
  Serial.printf("\n***MQTT CALLBACK: topic '%s', payload '%s'\n", topic, payload_buf);

  // Route to a subscriber property by RECORDING the message, not by dispatching it: the
  // dispatch publishes and runs driver code, and neither may happen inside this callback.
  // mqtt_process_queue() does it from loop_mqtt(). See the note on MqttQueueKind.
  if (is_settable_topic(topic)) {
    mqtt_queue_set(topic, payload_buf, length);
  } else if (_fallback_callback) {
    // No property owns this topic: hand it to the fallback (e.g. the controller). Pass
    // the already null-terminated payload_buf to avoid a second buffer.
    _fallback_callback(topic, (uint8_t*)payload_buf, length);
  }
}

// Open a broker session on the configured host. Returns true when connected.
// Called by setup_mqtt() and by loop_mqtt()'s reconnect path. On success it re-subscribes
// every settable topic (the session may be new) and then runs the reconnect callback,
// which re-publishes $state over the "lost" the broker may hold from the will.

static bool broker_connect() {
  if (!client_connected()) {
    Serial.println("MQTT: no network interface up, not connecting");
    return false;
  }
  if (mqtt_host[0] == '\0') {
    Serial.println("MQTT: no broker host, not connecting");
    return false;
  }

  // Transport selection may rewrite mqtt_port (POE-ISO without a CA falls back to 1883),
  // so port and TLS state are read only after it.
  if (transportClient == nullptr && !mqtt_init_transport()) {
    Serial.println("MQTT: transport setup failed, not connecting");
    return false;
  }
  Serial.printf("MQTT: connecting to %s:%u%s\n", mqtt_host, mqtt_port,
                mqtt_using_secure ? " [TLS]" : "");

  const int keepalive_s = 60;
  const int op_timeout_ms = mqtt_using_secure ? 2000 : 1000;
  // Socket connect/read/write timeout, 15 s for TLS and 10 s plain, set on the concrete
  // client. On 3.x it is NetworkClient::_timeout, reached only through
  // setConnectionTimeout(ms); setTimeout() there sets Stream's timeout, which the MQTT
  // library never uses. On 2.x WiFiClient::setTimeout(seconds) hides Stream's, so it
  // must not go through Client*, which would take the millisecond Stream one.
  const uint32_t socket_timeout_s = mqtt_using_secure ? 15 : 10;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (mqtt_using_secure) {
    secureClient.setConnectionTimeout(socket_timeout_s * 1000);
  } else {
    insecureClient.setConnectionTimeout(socket_timeout_s * 1000);
  }
#else
  if (mqtt_using_secure) {
    secureClient.setTimeout(socket_timeout_s);
  } else {
    insecureClient.setTimeout(socket_timeout_s);
  }
#endif
  // Clean session off: the broker keeps our subscriptions across a reconnect.
  mqtt_client.setOptions(keepalive_s, false, op_timeout_ms);
  Serial.printf("MQTT: keepalive %d s, operation timeout %d ms\n", keepalive_s, op_timeout_ms);

  // Will on the root device only; child devices share this connection and are treated
  // as lost with their root. The library keeps the topic pointer, so it is static.
  int n = snprintf(lwt_topic, sizeof(lwt_topic), "%s/%s/$state", HOMIE_TOPIC_PREFIX, getDeviceID());
  if (n < 0 || n >= (int)sizeof(lwt_topic)) {
    Serial.printf("MQTT: **ERROR -- will topic truncated to '%s'\n", lwt_topic);
  }
  mqtt_client.setWill(lwt_topic, HOMIE_STATE_LOST, true, homie_qos(true));
  Serial.printf("MQTT: will '%s' = '%s'\n", lwt_topic, HOMIE_STATE_LOST);

  ntp_lookup_wait();   // begin()/connect() resolve mqtt_host; see network.cpp
  mqtt_client.begin(mqtt_host, mqtt_port, *transportClient);
  mqtt_client.onMessageAdvanced(subscriber_callback);   // begin() clears it
  _mqtt_begun = true;

  bool ok;
  if (mqtt_username[0] == '\0') {
    ok = mqtt_client.connect(getDeviceID());
  } else {
    Serial.printf("MQTT: authenticating as '%s'\n", mqtt_username);
    ok = mqtt_client.connect(getDeviceID(), mqtt_username, mqtt_password);
  }
  if (!ok) {
    Serial.printf("MQTT: connect to %s failed, error %d, return code %d\n", mqtt_host,
                  (int)mqtt_client.lastError(), (int)mqtt_client.returnCode());
    return false;
  }
  Serial.printf("MQTT: connected to %s\n", mqtt_host);

  // QoS 0, the library default, which /set requires.
  for (int i = 0; i < num_subscribers; i++) {
    mqtt_client.subscribe(settable_properties[i].topic);
    Serial.printf("MQTT: subscribed '%s'\n", settable_properties[i].topic);
  }

  if (_reconnect_callback) _reconnect_callback();
  return true;
}

// True while mqtt_host is the mDNS-discovered broker's address, which loop_mqtt() may
// re-resolve; false for a configured broker.
static bool _broker_from_mdns = false;
static BrokerReconnect _reconnect_state = {0};

// Point mqtt_host/mqtt_port at the mDNS-discovered broker. Discovered brokers are used
// anonymously (temporary until credential negotiation), and the CA cert is re-read for
// the new host.
static void use_discovered_broker() {
  strncpy(mqtt_host, mdns_get_broker_host(), sizeof(mqtt_host) - 1);
  mqtt_host[sizeof(mqtt_host) - 1] = '\0';
  mqtt_port = mdns_get_broker_port();
  mqtt_invalidate_ca_cert();
  mqtt_username[0] = '\0';
  mqtt_password[0] = '\0';
  _broker_from_mdns = true;
}

void setup_mqtt() {
  _broker_from_mdns = false;
  broker_reconnect_succeeded(&_reconnect_state);
  // Resolve broker based on mqtt_mode
  if (mqtt_mode == MQTT_MODE_CONFIGURED_ONLY) {
    // Use only the configured broker
    if (!has_mqtt_broker()) {
      Serial.println("MQTT: **ERROR -- no broker configured (mqtt_mode=configured-only)");
      return;
    }
    Serial.println("MQTT: Using configured broker (mqtt_mode=configured-only)");
  } else if (mqtt_mode == MQTT_MODE_DISCOVERY_WITH_FALLBACK) {
    // Prefer mDNS-discovered broker, fall back to config
    if (mdns_has_broker()) {
      use_discovered_broker();
      Serial.printf("MQTT: Using mDNS broker %s:%u anonymous (mqtt_mode=discovery-with-fallback)\n",
                     mqtt_host, mqtt_port);
    } else if (has_mqtt_broker()) {
      Serial.println("MQTT: mDNS found nothing, using configured broker (mqtt_mode=discovery-with-fallback)");
    } else {
      Serial.println("MQTT: **ERROR -- no mDNS broker and no config broker (mqtt_mode=discovery-with-fallback)");
      return;
    }
  } else {
    // MQTT_MODE_DISCOVERY_ONLY (default) - only mDNS, ignore config
    if (mdns_has_broker()) {
      use_discovered_broker();
      Serial.printf("MQTT: Using mDNS broker %s:%u anonymous (mqtt_mode=discovery-only)\n",
                     mqtt_host, mqtt_port);
    } else {
      Serial.println("MQTT: **ERROR -- no broker discovered via mDNS (mqtt_mode=discovery-only)");
      return;
    }
  }

  Serial.printf("MQTT: %s broker %s:%u\n",
                 mqtt_is_secure() ? "MQTTS" : "MQTT", mqtt_host, mqtt_port);

  generateTopics();

  // Initialize transport (secure or insecure based on port)
  if (!mqtt_init_transport()) {
    Serial.println("MQTT: FAILED TO INITIALIZE TRANSPORT");
    return;
  }

  if (!broker_connect()) {
    delay(250);
    if (!broker_connect()) {
      delay(500);
      if (!broker_connect()) {
        Serial.println("MQTT: FAILED TO CONNECT");
        return;
      }
    }
  }
}

#define MQTT_RECONNECT_INTERVAL_MS 10000UL
#define MQTT_STATUS_INTERVAL_MS 30000UL

// Service the client, watch the link, reconnect when it is down, and drain the publish
// queue. Called from the main task on every pass, so inbound /set dispatch runs there.
void loop_mqtt() {
  static bool was_up = false;
  static uint32_t last_attempt_ms = 0;
  static uint32_t last_status_ms = 0;

  bool serviced = _mqtt_begun && mqtt_client.loop();
  bool up = serviced && mqtt_client.connected();
  uint32_t now = millis();

  if (up) {
    if (now - last_status_ms >= MQTT_STATUS_INTERVAL_MS) {
      last_status_ms = now;
      Serial.printf("MQTT: connected OK%s, up %lu s, broker %s, device %s\n",
                    mqtt_using_secure ? " [TLS]" : "", (unsigned long)(now / 1000),
                    mqtt_host, topic_device);
    }
    was_up = true;
  } else {
    if (was_up) {
      was_up = false;
      Serial.printf("MQTT: DISCONNECTED! error %d, loop %s, up %lu s\n",
                    (int)mqtt_client.lastError(), serviced ? "ok" : "failed",
                    (unsigned long)(now / 1000));
    }
    if (now - last_attempt_ms >= MQTT_RECONNECT_INTERVAL_MS) {
      last_attempt_ms = now;
      Serial.printf("MQTT: not connected (error %d), reconnecting to %s\n",
                    (int)mqtt_client.lastError(), mqtt_host);
      if (broker_connect()) {
        broker_reconnect_succeeded(&_reconnect_state);
      } else if (_broker_from_mdns && broker_reconnect_failed(&_reconnect_state)) {
        // The discovered broker may have a new DHCP lease: ask for its .local name again.
        Serial.printf("MQTT: %d reconnects to %s failed, re-resolving the broker\n",
                      BROKER_RERESOLVE_AFTER_FAILURES, mqtt_host);
        if (mdns_reresolve_broker()) {
          use_discovered_broker();
          transportClient = nullptr;   // re-select the transport and CA for the new address
          Serial.printf("MQTT: reconnecting to re-resolved broker %s:%u\n", mqtt_host, mqtt_port);
          if (broker_connect()) broker_reconnect_succeeded(&_reconnect_state);
        }
      }
    }
  }

  // Runs while disconnected too: queued items are then dropped and reported failed.
  mqtt_process_queue();
}

// Drop the session (a clean DISCONNECT, so the broker does not fire the will) and forget
// the transport, so the next connect re-selects it for the possibly changed port and CA.
// The caller follows with setup_mqtt().
void mqtt_restart() {
  if (mqtt_connected()) mqtt_client.disconnect();
  transportClient = nullptr;
}

boolean mqtt_connected() {
  return _mqtt_begun && mqtt_client.connected();
}
