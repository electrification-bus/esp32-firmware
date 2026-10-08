#pragma once
#include <stddef.h>
#include <stdint.h>
#include <homie/homie_limits.h>

// Arduino-free pieces of the Homie controller (src/homie/controller.cpp), shared with the
// host tests in test/native/test_controller_inbox: the inbox that carries messages out of
// the MQTT receive callback, and the topic parser.

// A domain is part of the "<domain>/<version>" prefix, so it is never longer than it.
#define CONTROLLER_DOMAIN_MAX HOMIE_TOPIC_PREFIX_MAX

// FIFO of MQTT messages held in a caller-owned byte arena. Each message takes
// 4 + topic + 1 + payload + 1 bytes, so one buffer holds a burst of small values or a
// few large $description payloads without a fixed slot size, and nothing is allocated.
// A message that does not fit is refused whole.
//
// Single task only: the receive callback that pushes runs inside the client calls made
// by the task that pops, so the two never overlap.
class ControllerInbox {
 public:
  // constexpr, so a static inbox needs no constructor call at startup; one would keep the
  // arena linked into device builds that never use the controller.
  constexpr ControllerInbox(uint8_t* arena, size_t size) : _arena(arena), _size(size) {}

  // Copy one message in. Returns false, storing nothing, when it does not fit.
  bool push(const char* topic, const uint8_t* payload, size_t length);
  // The oldest message. topic and payload point into the arena, NUL-terminated, and stay
  // valid until pop() or clear(). Returns false when empty.
  bool front(const char** topic, const char** payload, size_t* length) const;
  void pop();
  void clear();

  size_t count() const { return _count; }
  size_t used() const { return _used; }     // bytes held by stored messages
  size_t size() const { return _size; }

 private:
  uint8_t* _arena;
  size_t _size;
  size_t _head = 0;      // first byte of the oldest message
  size_t _tail = 0;      // where the next message goes
  size_t _limit = 0;     // end of the data before the wrap, while _wrapped
  bool _wrapped = false; // the newest messages start again at offset 0
  size_t _count = 0;
  size_t _used = 0;
};

enum ControllerTopicKind : uint8_t {
  CONTROLLER_TOPIC_STATE = 0,      // <domain>/<version>/<device>/$state
  CONTROLLER_TOPIC_DESCRIPTION,    // <domain>/<version>/<device>/$description
  CONTROLLER_TOPIC_PROPERTY,       // <domain>/<version>/<device>/<node>/<property>
};

struct ControllerTopic {
  ControllerTopicKind kind;
  char domain[CONTROLLER_DOMAIN_MAX + 1];
  char device_id[HOMIE_DEVICE_ID_MAX + 1];
  char node_id[HOMIE_NODE_ID_MAX + 1];           // empty unless a property
  char property_id[HOMIE_PROPERTY_ID_MAX + 1];   // empty unless a property
};

// Split a topic the controller handles. Returns false for any other topic (other device
// attributes, $-prefixed node levels such as $alert, /set and $target topics) and for a
// topic with an empty level or one longer than its homie_limits.h maximum, so an id is
// never stored truncated.
bool controller_parse_topic(const char* topic, ControllerTopic* out);
