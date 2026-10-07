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
        if (w + 1 >= out_size - 1) break;  // no room for the hyphen AND a char after it
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

// Drop hyphens left at the end of `s` when a "%s-%s" join was truncated.
static void strip_trailing_hyphens(char* s) {
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == '-') s[--n] = '\0';
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
    strip_trailing_hyphens(out);
  } else {
    // No usable name — fall back to a bare MAC id rather than a leading hyphen.
    snprintf(out, out_size, "%s", mac_id);
  }
}

// Append s into dst[w..cap), returns new write index (null-terminates not required here).
static size_t append_bounded(char* dst, size_t w, size_t cap, const char* s) {
  while (*s && w < cap - 1) dst[w++] = *s++;
  return w;
}

void resolve_device_id(const char* templ, const uint8_t* mac6, const char* name,
                       char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (!templ || templ[0] == '\0') templ = "${chip_id}";   // default: bare chip id
  char chip_full[16] = {0}, chip_short[8] = {0}, name_id[48] = {0};
  format_mac_as_id(mac6, 6, chip_full, sizeof(chip_full));
  format_mac_as_id(mac6 + 3, 3, chip_short, sizeof(chip_short));
  sanitize_homie_id(name ? name : "", name_id, sizeof(name_id));
  char raw[128] = {0};
  size_t w = 0;
  for (const char* p = templ; *p && w < sizeof(raw) - 1; ) {
    // ${...} substitution. Check ${chip_id_short} before ${chip_id} (prefix).
    if (strncmp(p, "${chip_id_short}", 16) == 0) { w = append_bounded(raw, w, sizeof(raw), chip_short); p += 16; }
    else if (strncmp(p, "${chip_id}", 10) == 0)  { w = append_bounded(raw, w, sizeof(raw), chip_full);  p += 10; }
    else if (strncmp(p, "${name}", 7) == 0)       { w = append_bounded(raw, w, sizeof(raw), name_id);    p += 7; }
    else { raw[w++] = *p++; }
  }
  raw[w] = '\0';
  sanitize_homie_id(raw, out, out_size);   // coerce the literal parts to Homie-legal too
}

void make_homie_child_id(const char* parent_id, const char* suffix,
                         char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  char suffix_id[48] = {0};
  sanitize_homie_id(suffix, suffix_id, sizeof(suffix_id));   // sanitize the new segment only
  if (parent_id && parent_id[0] != '\0' && suffix_id[0] != '\0') {
    snprintf(out, out_size, "%s-%s", parent_id, suffix_id);
    strip_trailing_hyphens(out);
  } else if (parent_id && parent_id[0] != '\0') {
    snprintf(out, out_size, "%s", parent_id);               // empty suffix -> just the parent
  } else {
    snprintf(out, out_size, "%s", suffix_id);
  }
}
