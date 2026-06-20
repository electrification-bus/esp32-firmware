#include <homie/homie_id.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// See homie_id.h. Single pass, no allocation: a hyphen is emitted lazily — only when
// a separator run is followed by another kept character — which collapses runs and
// strips leading/trailing hyphens in one go.
void sanitize_homie_id(const char* in, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  size_t w = 0;
  bool pending_hyphen = false;  // separator(s) seen since the last kept char
  bool emitted = false;         // at least one kept char written (suppresses leading hyphen)
  for (const char* p = in; p && *p; ++p) {
    unsigned char c = (unsigned char)*p;
    char lc = (char)tolower(c);
    bool keep = (lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9');
    if (keep) {
      if (pending_hyphen && emitted) {
        if (w >= out_size - 1) break;
        out[w++] = '-';
      }
      pending_hyphen = false;
      if (w >= out_size - 1) break;
      out[w++] = lc;
      emitted = true;
    } else if (c == '_' || c == '.' || c == '-' || isspace(c)) {
      pending_hyphen = true;    // collapse; trailing run is never flushed -> stripped
    }
    // any other character is dropped
  }
  out[w] = '\0';
}

void format_mac_as_id(const uint8_t* mac, size_t n, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  size_t w = 0;
  for (size_t i = 0; i < n && w + 2 < out_size; ++i) {
    w += snprintf(out + w, out_size - w, "%02x", mac[i]);  // lowercase hex
  }
  out[w] = '\0';
}

void make_homie_device_id(const char* name, const uint8_t* mac, size_t n,
                          char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  char name_id[48] = {0};
  char mac_id[16] = {0};
  sanitize_homie_id(name, name_id, sizeof(name_id));
  format_mac_as_id(mac, n, mac_id, sizeof(mac_id));
  if (name_id[0] != '\0') {
    snprintf(out, out_size, "%s-%s", name_id, mac_id);
  } else {
    // No usable name — fall back to a bare MAC id rather than a leading hyphen.
    snprintf(out, out_size, "%s", mac_id);
  }
}

void make_homie_child_id(const char* parent_id, const char* suffix,
                         char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  char suffix_id[48] = {0};
  sanitize_homie_id(suffix, suffix_id, sizeof(suffix_id));   // sanitize the new segment only
  if (parent_id && parent_id[0] != '\0' && suffix_id[0] != '\0') {
    snprintf(out, out_size, "%s-%s", parent_id, suffix_id);
  } else if (parent_id && parent_id[0] != '\0') {
    snprintf(out, out_size, "%s", parent_id);               // empty suffix -> just the parent
  } else {
    snprintf(out, out_size, "%s", suffix_id);
  }
}
