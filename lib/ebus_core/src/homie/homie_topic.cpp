// SPDX-License-Identifier: MIT
// The run-time topic prefix; see homie_set_topic_domain() in homie/homie.h.
#include <homie/homie.h>
#include <stdio.h>
#include <string.h>

static char _topic_prefix[HOMIE_TOPIC_PREFIX_MAX + 1] = HOMIE_TOPIC_PREFIX;

bool homie_set_topic_domain(const char* domain) {
    if (!domain || !domain[0]) return false;
    for (const char* c = domain; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
    }
    char prefix[HOMIE_TOPIC_PREFIX_MAX + 2];
    int n = snprintf(prefix, sizeof(prefix), "%s/%s", domain, HOMIE_VERSION_NUM);
    if (n < 0 || n > HOMIE_TOPIC_PREFIX_MAX) return false;
    memcpy(_topic_prefix, prefix, (size_t)n + 1);
    return true;
}

const char* homie_topic_prefix() { return _topic_prefix; }
