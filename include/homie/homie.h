#pragma once

#define HOMIE_VERSION_NUM       "5"
#define HOMIE_HOMIE_DOMAIN      "homie"
#define EBUS_HOMIE_DOMAIN       "ebus"

// Select topic domain at compile time: -DUSE_EBUS_TOPIC → "ebus/5", default → "homie/5"
#ifdef USE_EBUS_TOPIC
  #define HOMIE_TOPIC_DOMAIN    EBUS_HOMIE_DOMAIN
#else
  #define HOMIE_TOPIC_DOMAIN    HOMIE_HOMIE_DOMAIN
#endif

#define HOMIE_TOPIC_PREFIX      HOMIE_TOPIC_DOMAIN "/" HOMIE_VERSION_NUM
#define HOMIE_TOPIC_SET         "set"

// Buffer size (incl. null) for a Homie device/node `type` string. Homie itself
// leaves `type` free-form, but the eBus vocabulary uses long namespaced values.
// The enumerated registries live in the specification repo:
//   energy.ebus.device.*      → specification/registries/device-types.md
//   energy.ebus.capability.*  → specification/registries/capability-types.md
//   (https://github.com/electrification-bus/specification/tree/main/registries)
// As of this writing the longest device type is
// `energy.ebus.device.distribution-enclosure` (41) and the longest capability/node
// type is `energy.ebus.capability.shed-forecast` (36); 64 holds those with headroom
// for vocabulary growth. Optional convenience constants for the registered values live
// in homie/ebus_vocabulary.h (rrj.1) — type stays free-form on the wire. (rrj.5: the old Node[16]/
// Device[32] buffers truncated these; F2's snprintf made truncation safe but the
// values were still wrong.)
#define HOMIE_TYPE_MAXLEN       64

inline const char* top_level_topic() { return HOMIE_TOPIC_DOMAIN; }

#define HOMIE_DATATYPE_BOOLEAN  "boolean"
#define HOMIE_DATATYPE_STRING   "string"
#define HOMIE_DATATYPE_INTEGER  "integer"
#define HOMIE_DATATYPE_FLOAT    "float"
#define HOMIE_DATATYPE_ENUM     "enum"
#define HOMIE_DATATYPE_COLOR    "color"
#define HOMIE_DATATYPE_DATETIME "datetime"
#define HOMIE_DATATYPE_DURATION "duration"
#define HOMIE_DATATYPE_JSON     "json"

#define HOMIE_HOMIE       "homie"
#define HOMIE_NAME        "name"
#define HOMIE_STATE       "state"
#define HOMIE_NODES       "nodes"
#define HOMIE_ID          "id"
#define HOMIE_TYPE        "type"
#define HOMIE_DATATYPE    "datatype"
#define HOMIE_FORMAT      "format"
#define HOMIE_SETTABLE    "settable"
#define HOMIE_RETAINED    "retained"
#define HOMIE_ROUNDTO     "round_to"
#define HOMIE_UNIT        "unit"
#define HOMIE_VALUE       "value"
#define HOMIE_PROPERTIES  "properties"
#define HOMIE_CHILDREN    "children"
#define HOMIE_ROOT        "root"
#define HOMIE_PARENT      "parent"
#define HOMIE_VERSION     "version"
#define HOMIE_IMPLEMENTATION    "implementation"
#define HOMIE_$STATE       "$state"
#define HOMIE_$DESCRIPTION "$description"
#define HOMIE_$TARGET      "$target"

#define HOMIE_STATE_DISCONNECTED  "disconnected"
#define HOMIE_STATE_SLEEPING      "sleeping"
#define HOMIE_STATE_INIT          "init"
#define HOMIE_STATE_READY         "ready"
#define HOMIE_STATE_LOST          "lost"
#define HOMIE_STATE_UNKNOWN       "unknown"

