#include <homie/controller_inbox.h>
#include <string.h>

// Record layout: uint16 topic length, uint16 payload length, topic, NUL, payload, NUL.
// The header is copied with memcpy, so records need no alignment.
#define INBOX_HEADER 4
#define INBOX_FIELD_MAX 0xFFFF

static size_t record_size(size_t topic_len, size_t payload_len) {
  return INBOX_HEADER + topic_len + 1 + payload_len + 1;
}

bool ControllerInbox::push(const char* topic, const uint8_t* payload, size_t length) {
  size_t topic_len = strlen(topic);
  if (topic_len > INBOX_FIELD_MAX || length > INBOX_FIELD_MAX) return false;
  size_t need = record_size(topic_len, length);

  if (_count == 0) {
    _head = 0;
    _tail = 0;
    _wrapped = false;
  }

  size_t at;
  if (!_wrapped) {
    // Data occupies [_head, _tail): append, or start again at 0 ahead of _head.
    if (_size - _tail >= need) {
      at = _tail;
    } else if (_head >= need) {
      _limit = _tail;
      _wrapped = true;
      at = 0;
    } else {
      return false;
    }
  } else {
    // Data occupies [_head, _limit) and [0, _tail): the gap is [_tail, _head).
    if (_head - _tail < need) return false;
    at = _tail;
  }

  uint16_t header[2] = {(uint16_t)topic_len, (uint16_t)length};
  uint8_t* p = _arena + at;
  memcpy(p, header, INBOX_HEADER);
  p += INBOX_HEADER;
  memcpy(p, topic, topic_len);
  p[topic_len] = '\0';
  p += topic_len + 1;
  if (length > 0) memcpy(p, payload, length);
  p[length] = '\0';

  _tail = at + need;
  _count++;
  _used += need;
  return true;
}

bool ControllerInbox::front(const char** topic, const char** payload, size_t* length) const {
  if (_count == 0) return false;
  uint16_t header[2];
  memcpy(header, _arena + _head, INBOX_HEADER);
  const char* t = (const char*)(_arena + _head + INBOX_HEADER);
  if (topic) *topic = t;
  if (payload) *payload = t + header[0] + 1;
  if (length) *length = header[1];
  return true;
}

void ControllerInbox::pop() {
  if (_count == 0) return;
  uint16_t header[2];
  memcpy(header, _arena + _head, INBOX_HEADER);
  size_t n = record_size(header[0], header[1]);
  _head += n;
  _used -= n;
  _count--;
  if (_count == 0) {
    _head = 0;
    _tail = 0;
    _wrapped = false;
  } else if (_wrapped && _head == _limit) {
    _head = 0;
    _wrapped = false;
  }
}

void ControllerInbox::clear() {
  _head = 0;
  _tail = 0;
  _limit = 0;
  _wrapped = false;
  _count = 0;
  _used = 0;
}

// Copy [start, start + len) into out (capacity max + 1). False if empty or too long.
static bool copy_level(const char* start, size_t len, char* out, size_t max) {
  if (len == 0 || len > max) return false;
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

bool controller_parse_topic(const char* topic, ControllerTopic* out) {
  if (!topic || !out) return false;

  // At most 5 levels: domain, version, device, then $attribute or node/property.
  const char* level[5];
  size_t len[5];
  int n = 0;
  const char* p = topic;
  for (;;) {
    if (n == 5) return false;
    const char* slash = strchr(p, '/');
    level[n] = p;
    len[n] = slash ? (size_t)(slash - p) : strlen(p);
    n++;
    if (!slash) break;
    p = slash + 1;
  }
  if (n < 4) return false;
  if (len[1] == 0) return false;

  if (!copy_level(level[0], len[0], out->domain, CONTROLLER_DOMAIN_MAX)) return false;
  if (!copy_level(level[2], len[2], out->device_id, HOMIE_DEVICE_ID_MAX)) return false;
  out->node_id[0] = '\0';
  out->property_id[0] = '\0';

  if (n == 4) {
    if (len[3] == sizeof("$state") - 1 && memcmp(level[3], "$state", len[3]) == 0) {
      out->kind = CONTROLLER_TOPIC_STATE;
      return true;
    }
    if (len[3] == sizeof("$description") - 1 && memcmp(level[3], "$description", len[3]) == 0) {
      out->kind = CONTROLLER_TOPIC_DESCRIPTION;
      return true;
    }
    return false;
  }

  // n == 5: a property value, unless either level is a $ attribute ($alert/<id>, ...).
  if (level[3][0] == '$' || level[4][0] == '$') return false;
  if (!copy_level(level[3], len[3], out->node_id, HOMIE_NODE_ID_MAX)) return false;
  if (!copy_level(level[4], len[4], out->property_id, HOMIE_PROPERTY_ID_MAX)) return false;
  out->kind = CONTROLLER_TOPIC_PROPERTY;
  return true;
}
