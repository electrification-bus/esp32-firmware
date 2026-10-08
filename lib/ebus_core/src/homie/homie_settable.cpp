// SPDX-License-Identifier: MIT
// The settable table and /set dispatch; see homie/homie_settable.h. The port supplies the
// table's storage and calls settable_dispatch() from its own task, never from inside the
// MQTT receive callback.
#include <homie/homie_settable.h>
#include <homie/homie_log.h>
#include <homie/homie_transport.h>
#include <homie/Property.h>
#include <stdio.h>
#include <string.h>

static subscribed_settable_property_t* _table = nullptr;
static int _capacity = 0;
static int _count = 0;
static HomieTransport* _transport = nullptr;
static settable_entity_call_t _entity_call = nullptr;

void settable_table_bind(subscribed_settable_property_t* storage, int capacity,
                         HomieTransport* transport, settable_entity_call_t entity_call) {
  _table = storage;
  _capacity = storage ? capacity : 0;
  _count = 0;
  _transport = transport;
  _entity_call = entity_call;
  for (int i = 0; i < _capacity; i++) _table[i] = subscribed_settable_property_t();
}

int settable_count() { return _count; }

const char* settable_topic(int i) { return (i >= 0 && i < _count) ? _table[i].topic : nullptr; }

bool settable_subscribe_now(const char* topic) {
  if (!_transport || !_transport->connected()) return false;
  return _transport->subscribe(topic, 0);   // /set is non-retained -> QoS 0 (C4)
}

static bool table_bound(const char* topic) {
  if (_table) return true;
  homie_logf("MQTT: **ERROR -- settable subscriber table not set up (settable_table_bind); "
             "/set for '%s' NOT registered\n", topic);
  return false;
}

// Every registration path refuses a topic once the table is full; this says how to fix it.
static void report_settable_table_full(const char* topic) {
  homie_logf("MQTT: **ERROR -- settable subscriber table full (%d); /set for '%s' NOT "
             "registered. ./ebus-esp32 generate sizes it from the settable properties in "
             "device.yml; for entries added at runtime, build with "
             "-DMAX_SETTABLE_SUBSCRIBERS=<n> in build_src_flags\n", _capacity, topic);
}

// A table entry holds the topic in a fixed char[]; refuse one that does not fit rather
// than truncate it (a truncated key never matches an inbound /set).
static bool settable_topic_fits(const char* topic) {
  size_t len = strlen(topic);
  if (len < sizeof(_table[0].topic)) return true;
  homie_logf("MQTT: /set topic is %u bytes, over the %u-byte limit; not registered: %s\n",
             (unsigned)len, (unsigned)(sizeof(_table[0].topic) - 1), topic);
  return false;
}

void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance) {
   if (!table_bound(topic) || !settable_topic_fits(topic)) return;
   // Check if topic already registered (avoid duplicates on reconnection)
   for(int i=0; i<_count; i++) {
     if (strcmp(_table[i].topic, topic) == 0) {
       // Already registered, update callback/instance in case they changed
       _table[i].property_callback = cb;
       _table[i].instance = instance;
       return;
     }
   }
   if (_count >= _capacity) {
     report_settable_table_full(topic);
     return;
   }
   // New subscription. Null the entity fields explicitly: an entry created here may
   // never be visited by entity_subscribe_for_callbacks(), and dispatch branches on them.
   snprintf(_table[_count].topic, sizeof(_table[0].topic), "%s", topic);
   _table[_count].property_callback = cb;
   _table[_count].instance = instance;
   _table[_count].entity_callback = nullptr;
   _table[_count].entity = nullptr;
   _count++;
}

// D3: register a per-property fn-ptr handler (+context) for a /set topic. Updates an
// existing entry's handler in place (reconnect-safe), else appends a new subscriber.
void subscribe_settable_handler(const char* topic, settable_handler_t handler, void* ctx) {
   if (!table_bound(topic) || !settable_topic_fits(topic)) return;
   for (int i = 0; i < _count; i++) {
     if (strcmp(_table[i].topic, topic) == 0) {
       _table[i].handler = handler;
       _table[i].handler_ctx = ctx;
       return;
     }
   }
   if (_count >= _capacity) {
     report_settable_table_full(topic);
     return;
   }
   // Null the property/entity fields explicitly. A topic registered ONLY here — a
   // NodeProperty::on_set() handler, or a watch on a device attribute like $state — has
   // no Property or NodeEntity behind it, and dispatch tests both before calling.
   snprintf(_table[_count].topic, sizeof(_table[0].topic), "%s", topic);
   _table[_count].handler = handler;
   _table[_count].handler_ctx = ctx;
   _table[_count].property_callback = nullptr;
   _table[_count].instance = nullptr;
   _table[_count].entity_callback = nullptr;
   _table[_count].entity = nullptr;
   _count++;
}


void entity_subscribe_for_callbacks(const char* topic, entity_settable_callback_t cb, NodeEntity* entity) {
   if (!table_bound(topic) || !settable_topic_fits(topic)) return;
   for(int i=0;i<_count;i++) {
     if (strcmp(_table[i].topic, topic) == 0) {
       //add entity pointer
       homie_logf("ENTITY: Callback Subscription: %s SUCCESS\n", topic);
       _table[i].entity_callback = cb;
       _table[i].entity = entity;
       return;
     }
   }
   // Topic not found — create entry (Property::subscribe may not have run yet)
   if (_count < _capacity) {
     homie_logf("ENTITY: Callback Subscription: %s CREATED NEW\n", topic);
     snprintf(_table[_count].topic, sizeof(_table[0].topic), "%s", topic);
     _table[_count].property_callback = nullptr;
     _table[_count].instance = nullptr;
     _table[_count].entity_callback = cb;
     _table[_count].entity = entity;
     _count++;
     settable_subscribe_now(topic);
   } else {
     report_settable_table_full(topic);
   }
}

// Calls the driver through the caller bound with the table, which is defined where
// NodeEntity is: this file needs NodeEntity only as a pointer.
static bool call_entity(subscribed_settable_property_t* s, Property* instance,
                        const char* property_id, const char* value) {
  if (!_entity_call) {
    homie_logf("MQTT: **ERROR -- no entity caller bound (settable_table_bind); '%s' refused\n",
               s->topic);
    return false;
  }
  return _entity_call(s->entity, s->entity_callback, instance, property_id, value);
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
      homie_logf("MQTT: Invoking settable handler for topic '%s'\n", topic);
      accepted = s->handler(s->handler_ctx, value);
  }
  if (accepted && s->entity_callback && s->entity) {
      if (s->instance) {
          // Homie Property exists — use Property* overload
          homie_logf("MQTT: Invoking entity callback for topic '%s'\n", topic);
          accepted = call_entity(s, s->instance, nullptr, value);
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
              homie_logf("MQTT: Invoking entity callback '%s'='%s'\n", pid, value);
              accepted = call_entity(s, nullptr, pid, value);
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
// The port runs an inbound /set through here, and deliver_local_set() takes the IDENTICAL
// route. One dispatch path, so a driver never needs to care where a set came from. Both
// must run on the main task (see deliver_local_set() in homie/homie_settable.h), which is
// what lets the snapshot go without a lock.
bool settable_dispatch(const char* topic, const char* value) {
  bool handled = false;
  for(int i=0;i<_count;i++) {
    subscribed_settable_property_t* s = &_table[i];
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
      homie_logf("MQTT: '%s' = '%s' refused by its driver, not published\n", topic, value);
    }
  }
  return handled;
}

// Is a settable property registered for this topic? The port's receive callback defers
// the dispatch but must still decide NOW whether the message is ours or the fallback's,
// because the payload buffer is only valid until it returns.
bool settable_is_registered(const char* topic) {
  for (int i = 0; i < _count; i++) {
    if (strcmp(_table[i].topic, topic) == 0) return true;
  }
  return false;
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
  homie_logf("MQTT: local set '%s' = '%s'\n", topic, value);
  bool handled = settable_dispatch(topic, value);
  if (!handled) {
    homie_logf("MQTT: local set NOT DELIVERED — no settable property registered for '%s'\n", topic);
  }
  return handled;
}
