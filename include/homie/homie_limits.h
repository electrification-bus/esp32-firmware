#pragma once
// Longest Homie ids and topics the firmware holds, in characters. Every buffer that holds
// one is sized from these (one more byte, for the NUL), so a topic that fits one buffer
// fits all of them: the property topic, its /set and $target forms, the publish queue
// item and the settable table entry.
//
// ./ebus-esp32 generate reads the four base values and refuses a device.yml whose ids or
// topics exceed them, so each must stay a plain integer literal on its own #define line.

#define HOMIE_DEVICE_ID_MAX    63    // a root or child device id
#define HOMIE_NODE_ID_MAX      31
#define HOMIE_PROPERTY_ID_MAX  31
#define HOMIE_TOPIC_MAX        127   // any topic the firmware builds, publishes or queues

// "<domain>/<version>": "homie/5" is the longer of the two (see HOMIE_TOPIC_PREFIX).
#define HOMIE_TOPIC_PREFIX_MAX 7

// "<prefix>/<device-id>/", the form Device::topic() returns.
#define HOMIE_DEVICE_TOPIC_MAX (HOMIE_TOPIC_PREFIX_MAX + 1 + HOMIE_DEVICE_ID_MAX + 1)

// "<prefix>/<device-id>/<node-id>/<property-id>", leaving room in HOMIE_TOPIC_MAX for
// "/$target", the longest suffix appended to it ("/set" is shorter).
#define HOMIE_PROPERTY_TOPIC_MAX (HOMIE_TOPIC_MAX - (int)(sizeof("/$target") - 1))
