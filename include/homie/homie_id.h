#pragma once
#include <stddef.h>
#include <stdint.h>

// Coerce an arbitrary string into a Homie-legal topic id (rrj.2).
//
// Homie 5 (convention §"Topic IDs"): a topic level id MAY ONLY contain lowercase
// letters `a`-`z`, digits `0`-`9`, and the hyphen `-`. Vendor-supplied node ids
// (e.g. `environment_sht20`) often carry underscores, dots, spaces, or uppercase,
// which are illegal on the wire. This mirrors the python-sdk sanitize_homie_id():
//   1. lowercase
//   2. map underscores, whitespace, and dots to hyphens
//   3. drop any other character outside [a-z0-9-]
//   4. collapse runs of hyphens
//   5. strip leading/trailing hyphens
//
// Writes the sanitized id (null-terminated, truncated to out_size) into `out`.
// An all-illegal or empty input yields an empty string — the caller decides how to
// handle that (keep a fallback id, warn, etc.). Note the node `$type` is NOT an id
// and keeps its dotted reverse-domain form (energy.ebus.capability.*) — only ids are
// sanitized.
void sanitize_homie_id(const char* in, char* out, size_t out_size);

// --- Unique-id generation toolkit (rrj.2) ---
// Hardcoding unique device-ids doesn't scale; derive them from hardware instead.

// Format `n` MAC bytes as lowercase hex (a Homie-legal id fragment). E.g. the bytes
// {0x90,0xF5,0x70} -> "90f570". `out` needs at least 2*n+1 bytes. The common case is
// the last 3 octets of the STA MAC for a short, board-unique suffix.
void format_mac_as_id(const uint8_t* mac, size_t n, char* out, size_t out_size);

// Compose a Homie-legal device-id from a human name and a unique MAC suffix:
//   sanitize_homie_id(name) + "-" + format_mac_as_id(mac, n)
// e.g. ("EBUS", {..,0x90,0xF5,0x70}, 3) -> "ebus-90f570". This is the canonical
// "stable name + per-unit hardware suffix" pattern — use it instead of hardcoding.
void make_homie_device_id(const char* name, const uint8_t* mac, size_t n,
                          char* out, size_t out_size);

// Resolve a device-id TEMPLATE into a Homie-legal id (runtime — the chip id is only
// known on-device). ${...} substitution, then sanitize the whole result:
//   ${chip_id}        full 6-byte eFuse MAC, lowercase hex (12 chars; globally unique)
//   ${chip_id_short}  last 3 bytes (6 chars; shorter, but OUI-dependent collisions)
//   ${name}           sanitize_homie_id(name)
// An empty/null template defaults to "${chip_id}". `mac6` is the 6 MAC bytes in standard
// order. (${...} chosen over {...} so device.yml needs no quoting — '$' isn't a YAML
// indicator.) Device-ids are OPAQUE to consumers (they use $type + info/serial-number
// for identity), so the template is purely a publisher-side convenience.
void resolve_device_id(const char* templ, const uint8_t* mac6, const char* name,
                       char* out, size_t out_size);

// Compose a child device-id by appending a sanitized suffix to an (already-legal)
// parent/root id: `parent_id` + "-" + sanitize_homie_id(suffix). For nested devices
// that share the parent's chip-id, e.g. ("ebus-a1b2c3d4e5f6", "phase a") ->
// "ebus-a1b2c3d4e5f6-phase-a". Each segment is sanitized independently (joining first
// then sanitizing is NOT equivalent — a hyphen joiner can be collapsed). `parent_id`
// is assumed already Homie-legal and is copied verbatim.
void make_homie_child_id(const char* parent_id, const char* suffix,
                         char* out, size_t out_size);
