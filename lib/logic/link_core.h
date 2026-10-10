#pragma once
// The platform-free parts of logic/link: reference parsing, * matching, rounding and
// %-substitution. No Arduino dependency, so test/native/test_link_core compiles it on the
// host.
#include <stddef.h>
#include <stdint.h>

#include <homie/homie_limits.h>

static const size_t LINK_VALUE_MAX = 64;   // a source value, NUL included

// A failed delivery is retried after the poll interval, then after twice as long each
// time, up to LINK_RETRY_MAX_MS. A condition that persists (an unmatched pattern, a wait
// for discovery) is logged when it starts and again at most every LINK_REMINDER_MS.
static const uint32_t LINK_RETRY_MAX_MS = 30000;
static const uint32_t LINK_REMINDER_MS  = 60000;

// The wait before the next retry: `interval_ms` after the first failure (`current_ms` 0),
// then doubled each time, never over LINK_RETRY_MAX_MS (nor under `interval_ms`).
uint32_t link_next_retry_ms(uint32_t current_ms, uint32_t interval_ms);

// One reference, "<node>/<property>" or "<device-id>/<node>/<property>", split into
// Homie-legal parts. A part without '*' is sanitized as sanitize_homie_id() does; a part
// with one is a pattern and keeps it.
struct LinkRef {
    int  parts = 0;                              // 2 or 3
    bool wild  = false;                          // the device or node part is a pattern
    char dev[HOMIE_DEVICE_ID_MAX + 1]    = {0};  // 3 parts only
    char node[HOMIE_NODE_ID_MAX + 1]     = {0};
    char prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
};

enum LinkRefStatus {
    LINK_REF_OK = 0,
    LINK_REF_SHAPE,              // empty, an empty part, or not two or three parts
    LINK_REF_TOO_LONG,           // a part over HOMIE_DEVICE_ID_MAX / _NODE_ / _PROPERTY_
    LINK_REF_BAD_PATTERN,        // a pattern with a character other than a-z 0-9 - *
    LINK_REF_PROPERTY_PATTERN,   // '*' in the property, which is never resolved
};

// Parse exactly `len` bytes of `ref`, surrounding spaces ignored. `out` is filled only on
// LINK_REF_OK.
LinkRefStatus link_parse_ref(const char* ref, size_t len, LinkRef* out);

// What a status means, for a log line.
const char* link_ref_status_text(LinkRefStatus status);

// Does `s` match `pat`, where '*' in `pat` stands for any run of characters (including
// none)?
bool link_glob_match(const char* pat, const char* s);

// Binding a * reference: every discovered device/node pair that matches the reference's
// device and node patterns and has the property is passed to link_resolve_add(), and the
// reference binds only if exactly one did.
enum LinkResolveResult { LINK_RESOLVE_NONE, LINK_RESOLVE_ONE, LINK_RESOLVE_MANY };

struct LinkResolve {
    int  hits = 0;
    char dev[HOMIE_DEVICE_ID_MAX + 1] = {0};    // the first match
    char node[HOMIE_NODE_ID_MAX + 1]  = {0};
    char cands[192] = {0};                      // "dev/node, dev/node", for one log line
};

// Does the discovered `dev_id`/`node_id` match the patterns? A part without '*' must be
// equal.
bool link_ref_matches(const char* dev_pat, const char* node_pat, const char* dev_id,
                      const char* node_id);
void link_resolve_add(LinkResolve* r, const char* dev_id, const char* node_id);
LinkResolveResult link_resolve_result(const LinkResolve* r);

// Store a source's new value in `dst` (`dst_size` bytes, cut on a UTF-8 boundary), unless
// there is none: `present` false (a controller's copy whose retained value was removed) or
// an empty `value` (a retraction, or no reading yet). Returns whether it stored, so the
// caller keeps the last good value otherwise.
bool link_take_value(char* dst, size_t dst_size, const char* value, bool present);

// `value` rounded to `decimals` places (at most 6) if it is a plain decimal number under
// 1e15 in magnitude, else copied as is. A negative `decimals` copies every value as is.
void link_rounded(const char* value, int decimals, char* out, size_t out_len);

// `format` with %1..%3 replaced by values[0..2] (each through link_rounded()), %s by
// values[0] and %% by '%'. A placeholder past `num_values` is left as written.
void link_render(const char* format, const char* const* values, int num_values, int decimals,
                 char* out, size_t out_len);
