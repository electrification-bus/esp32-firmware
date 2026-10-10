// Host-side tests for lib/logic/link_core.cpp: logic/link's reference parsing, * matching,
// rounding and %-substitution.
#include <unity.h>
#include "../../../lib/logic/link_core.cpp"

#include <string.h>

// ── UTF-8 cuts (util/utf8.h) ────────────────────────────────────────────────

// "a°b€": a(1) °(2: C2 B0) b(1) €(3: E2 82 AC)
static const char* const MIXED = "a\xC2\xB0" "b\xE2\x82\xAC";

static void test_utf8_fit_never_splits_a_character(void) {
  TEST_ASSERT_EQUAL_UINT(7, utf8_fit(MIXED, 7, 10));   // all fits
  TEST_ASSERT_EQUAL_UINT(7, utf8_fit(MIXED, 7, 7));
  TEST_ASSERT_EQUAL_UINT(4, utf8_fit(MIXED, 7, 6));    // inside the euro sign
  TEST_ASSERT_EQUAL_UINT(4, utf8_fit(MIXED, 7, 5));
  TEST_ASSERT_EQUAL_UINT(4, utf8_fit(MIXED, 7, 4));
  TEST_ASSERT_EQUAL_UINT(3, utf8_fit(MIXED, 7, 3));
  TEST_ASSERT_EQUAL_UINT(1, utf8_fit(MIXED, 7, 2));    // inside the degree sign
  TEST_ASSERT_EQUAL_UINT(0, utf8_fit(MIXED, 7, 0));
}

static void test_utf8_copy(void) {
  char out[6];
  TEST_ASSERT_EQUAL_UINT(4, utf8_copy(out, sizeof(out), MIXED));
  TEST_ASSERT_EQUAL_STRING("a\xC2\xB0" "b", out);
  char tiny[2];
  TEST_ASSERT_EQUAL_UINT(1, utf8_copy(tiny, sizeof(tiny), MIXED));
  TEST_ASSERT_EQUAL_STRING("a", tiny);
}

// ── link_glob_match ──────────────────────────────────────────────────────────

static void test_glob_literal_and_star(void) {
  TEST_ASSERT_TRUE(link_glob_match("environment-bme280", "environment-bme280"));
  TEST_ASSERT_FALSE(link_glob_match("environment-bme280", "environment-bme28"));
  TEST_ASSERT_TRUE(link_glob_match("*", "b0b21c90f570"));
  TEST_ASSERT_TRUE(link_glob_match("*", ""));
  TEST_ASSERT_TRUE(link_glob_match("environment-*", "environment-sht20"));
  TEST_ASSERT_TRUE(link_glob_match("environment-*", "environment-"));
  TEST_ASSERT_FALSE(link_glob_match("environment-*", "display-gme12864"));
  TEST_ASSERT_TRUE(link_glob_match("*-sht20", "environment-sht20"));
  TEST_ASSERT_TRUE(link_glob_match("e*-*0", "environment-sht20"));
  TEST_ASSERT_FALSE(link_glob_match("e*-*1", "environment-sht20"));
}

// ── binding a * reference ───────────────────────────────────────────────────

// A discovered fleet: device/node pairs that have the property being resolved.
struct Pair { const char* dev; const char* node; };

// What resolve_ref() in link.cpp does: every matching pair is added.
static LinkResolveResult resolve(const char* dev_pat, const char* node_pat, const Pair* fleet,
                                 int n, LinkResolve* r) {
  for (int i = 0; i < n; i++) {
    if (link_ref_matches(dev_pat, node_pat, fleet[i].dev, fleet[i].node)) {
      link_resolve_add(r, fleet[i].dev, fleet[i].node);
    }
  }
  return link_resolve_result(r);
}

static const Pair FLEET[] = {
  {"b0b21c90f570", "environment-bme280"},
  {"a4cf12e8d0b4", "environment-sht20"},
  {"a4cf12e8d0b4", "display-gme12864"},
};

static void test_resolve_star_device(void) {
  LinkResolve r;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_ONE, resolve("*", "environment-bme280", FLEET, 3, &r));
  TEST_ASSERT_EQUAL_STRING("b0b21c90f570", r.dev);
  TEST_ASSERT_EQUAL_STRING("environment-bme280", r.node);
}

static void test_resolve_star_in_both(void) {
  LinkResolve r;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_MANY, resolve("*", "environment-*", FLEET, 3, &r));
  TEST_ASSERT_EQUAL_STRING("b0b21c90f570/environment-bme280, a4cf12e8d0b4/environment-sht20",
                           r.cands);
}

static void test_resolve_honors_the_device_pattern(void) {
  LinkResolve r;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_ONE, resolve("a4cf*", "environment-*", FLEET, 3, &r));
  TEST_ASSERT_EQUAL_STRING("a4cf12e8d0b4", r.dev);
  TEST_ASSERT_EQUAL_STRING("environment-sht20", r.node);
  LinkResolve r2;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_ONE, resolve("b0b21c90f570", "environment-*", FLEET, 3, &r2));
  TEST_ASSERT_EQUAL_STRING("environment-bme280", r2.node);
  LinkResolve r3;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_NONE, resolve("ffff*", "*", FLEET, 3, &r3));
}

static void test_resolve_two_nodes_on_one_device_are_ambiguous(void) {
  LinkResolve r;
  TEST_ASSERT_EQUAL_INT(LINK_RESOLVE_MANY, resolve("a4cf12e8d0b4", "*", FLEET, 3, &r));
  TEST_ASSERT_EQUAL_STRING("a4cf12e8d0b4/environment-sht20, a4cf12e8d0b4/display-gme12864",
                           r.cands);
}

// ── link_parse_ref ───────────────────────────────────────────────────────────

static LinkRefStatus parse(const char* ref, LinkRef* r) {
  return link_parse_ref(ref, strlen(ref), r);
}

static void test_parse_two_and_three_parts(void) {
  LinkRef r;
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse("environment-sht20/temperature", &r));
  TEST_ASSERT_EQUAL_INT(2, r.parts);
  TEST_ASSERT_EQUAL_STRING("", r.dev);
  TEST_ASSERT_EQUAL_STRING("environment-sht20", r.node);
  TEST_ASSERT_EQUAL_STRING("temperature", r.prop);
  TEST_ASSERT_FALSE(r.wild);
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse(" b0b21c90f570/display/one-line ", &r));
  TEST_ASSERT_EQUAL_INT(3, r.parts);
  TEST_ASSERT_EQUAL_STRING("b0b21c90f570", r.dev);
  TEST_ASSERT_EQUAL_STRING("display", r.node);
  TEST_ASSERT_EQUAL_STRING("one-line", r.prop);
}

static void test_parse_takes_only_len_bytes(void) {
  LinkRef r;
  const char* list = "a/b, c/d/e";
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, link_parse_ref(list, 3, &r));
  TEST_ASSERT_EQUAL_STRING("a", r.node);
  TEST_ASSERT_EQUAL_STRING("b", r.prop);
}

static void test_parse_sanitizes_plain_parts(void) {
  LinkRef r;
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse("Environment_SHT20/Temperature", &r));
  TEST_ASSERT_EQUAL_STRING("environment-sht20", r.node);
  TEST_ASSERT_EQUAL_STRING("temperature", r.prop);
}

static void test_parse_patterns(void) {
  LinkRef r;
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse("*/environment-*/air-pressure", &r));
  TEST_ASSERT_TRUE(r.wild);
  TEST_ASSERT_EQUAL_STRING("*", r.dev);
  TEST_ASSERT_EQUAL_STRING("environment-*", r.node);
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse("B0B2*/display/one-line", &r));
  TEST_ASSERT_TRUE(r.wild);
  TEST_ASSERT_EQUAL_STRING("b0b2*", r.dev);
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse("dev/*/state", &r));
  TEST_ASSERT_TRUE(r.wild);
}

static void test_parse_rejects_malformed_patterns(void) {
  LinkRef r;
  TEST_ASSERT_EQUAL_INT(LINK_REF_BAD_PATTERN, parse("*/environment_*/air-pressure", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_BAD_PATTERN, parse("dev.*/node/prop", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_BAD_PATTERN, parse("* x/node/prop", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_PROPERTY_PATTERN, parse("*/node/air-*", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_PROPERTY_PATTERN, parse("node/*", &r));
}

static void test_parse_rejects_bad_shapes(void) {
  LinkRef r;
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("   ", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("a", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("a//b", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("/a/b", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("a/b/", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("a/b/c/d", &r));
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("!!!/b", &r));   // sanitizes to nothing
  TEST_ASSERT_EQUAL_INT(LINK_REF_SHAPE, parse("+/node/prop", &r));
}

// A string of `n` copies of `c`.
static const char* run_of(char c, size_t n) {
  static char buf[256];
  memset(buf, c, n);
  buf[n] = '\0';
  return buf;
}

static void test_parse_checks_each_part_against_its_limit(void) {
  LinkRef r;
  char ref[300];
  snprintf(ref, sizeof(ref), "%s/n/p", run_of('d', HOMIE_DEVICE_ID_MAX));
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse(ref, &r));
  snprintf(ref, sizeof(ref), "%s/n/p", run_of('d', HOMIE_DEVICE_ID_MAX + 1));
  TEST_ASSERT_EQUAL_INT(LINK_REF_TOO_LONG, parse(ref, &r));
  snprintf(ref, sizeof(ref), "%s*/n/p", run_of('d', HOMIE_DEVICE_ID_MAX));
  TEST_ASSERT_EQUAL_INT(LINK_REF_TOO_LONG, parse(ref, &r));
  snprintf(ref, sizeof(ref), "d/%s/p", run_of('n', HOMIE_NODE_ID_MAX));
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse(ref, &r));
  snprintf(ref, sizeof(ref), "d/%s/p", run_of('n', HOMIE_NODE_ID_MAX + 1));
  TEST_ASSERT_EQUAL_INT(LINK_REF_TOO_LONG, parse(ref, &r));
  snprintf(ref, sizeof(ref), "%s/p", run_of('n', HOMIE_NODE_ID_MAX + 1));   // two parts: node
  TEST_ASSERT_EQUAL_INT(LINK_REF_TOO_LONG, parse(ref, &r));
  snprintf(ref, sizeof(ref), "n/%s", run_of('p', HOMIE_PROPERTY_ID_MAX));
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse(ref, &r));
  snprintf(ref, sizeof(ref), "n/%s", run_of('p', HOMIE_PROPERTY_ID_MAX + 1));
  TEST_ASSERT_EQUAL_INT(LINK_REF_TOO_LONG, parse(ref, &r));
  // Measured after sanitizing: separators collapse.
  snprintf(ref, sizeof(ref), "n/%s__x", run_of('p', HOMIE_PROPERTY_ID_MAX - 2));
  TEST_ASSERT_EQUAL_INT(LINK_REF_OK, parse(ref, &r));
}

static void test_status_text(void) {
  TEST_ASSERT_NOT_NULL(strstr(link_ref_status_text(LINK_REF_TOO_LONG), "node 31"));
  TEST_ASSERT_EQUAL_STRING("* in the property is never resolved",
                           link_ref_status_text(LINK_REF_PROPERTY_PATTERN));
}

// ── link_rounded ─────────────────────────────────────────────────────────────

static void expect_rounded(const char* in, int decimals, const char* expected) {
  char out[LINK_VALUE_MAX];
  link_rounded(in, decimals, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out, in);
}

static void test_rounded_numbers(void) {
  expect_rounded("21.37", 1, "21.4");
  expect_rounded("49.6", 0, "50");
  expect_rounded("-0.04", 1, "0.0");
  expect_rounded("-3.26", 1, "-3.3");
  expect_rounded("7", 2, "7.00");
  expect_rounded("21.37", -1, "21.37");
  expect_rounded("1.23456789", 9, "1.234568");   // capped at 6 places
}

static void test_rounded_plain_decimal_forms(void) {
  expect_rounded("+5", 1, "5.0");
  expect_rounded(".5", 1, "0.5");
  expect_rounded("5.", 0, "5");
  expect_rounded("1e5", 1, "100000.0");
  expect_rounded("1E-3", 3, "0.001");
  expect_rounded("2.5e+01", 0, "25");
  expect_rounded("999999999999999", 0, "999999999999999");
}

static void test_rounded_passes_text_through(void) {
  expect_rounded("on", 1, "on");
  expect_rounded("21.4 C", 1, "21.4 C");
  expect_rounded("", 1, "");
  expect_rounded("0x1A", 1, "0x1A");
  expect_rounded("nan", 1, "nan");
  expect_rounded("-inf", 1, "-inf");
  expect_rounded("infinity", 1, "infinity");
  expect_rounded(" 21.4", 1, " 21.4");
  expect_rounded("21.4 ", 1, "21.4 ");
  expect_rounded("1e300", 1, "1e300");          // three exponent digits
  expect_rounded("1e20", 1, "1e20");            // over 1e15
  expect_rounded("-1000000000000000", 0, "-1000000000000000");
  expect_rounded("1e", 1, "1e");
  expect_rounded("-", 1, "-");
  expect_rounded(".", 1, ".");
  expect_rounded("1.2.3", 1, "1.2.3");
}

static void test_rounded_never_overflows_a_small_buffer(void) {
  char out[4];
  link_rounded("123.456", 2, out, sizeof(out));   // "123.46" does not fit: copied as is, cut
  TEST_ASSERT_EQUAL_STRING("123", out);
}

// ── link_render ──────────────────────────────────────────────────────────────

static void expect_render(const char* format, const char* const* values, int n, int decimals,
                          const char* expected) {
  char out[128];
  link_render(format, values, n, decimals, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out, format);
}

static void test_render_placeholders(void) {
  const char* v[] = {"21.37", "48.25", "on"};
  expect_render("%1 \xC2\xB0" "C|%2 %%", v, 2, 1, "21.4 \xC2\xB0" "C|48.2 %");
  expect_render("%s", v, 1, -1, "21.37");
  expect_render("%3/%2/%1", v, 3, 0, "on/48/21");
  expect_render("%2 stays", v, 1, -1, "%2 stays");   // no second source
  expect_render("100%", v, 1, -1, "100%");
  expect_render("%d %x", v, 1, -1, "%d %x");         // never reaches printf
}

static void test_render_stops_at_the_buffer(void) {
  const char* v[] = {"abcdefgh"};
  char out[6];
  link_render("%1", v, 1, -1, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("abcde", out);
}

static void test_render_cuts_on_a_character_boundary(void) {
  const char* v[] = {"21.4"};
  char out[7];
  // "21.4 °C" is 8 bytes; 6 fit, and the 6th would be half of the degree sign.
  link_render("%1 \xC2\xB0" "C", v, 1, -1, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("21.4 ", out);
  // The same cut inside a substituted value, then nothing after it.
  const char* w[] = {"ab\xE2\x82\xAC"};
  char out2[5];
  link_render("%1xyz", w, 1, -1, out2, sizeof(out2));
  TEST_ASSERT_EQUAL_STRING("ab", out2);
}

static void test_long_values_are_cut_on_a_character_boundary(void) {
  // 62 ASCII bytes and a 2-byte character: 64 bytes, one more than a value holds.
  char in[80];
  memset(in, 'x', 62);
  memcpy(in + 62, "\xC2\xB0", 3);
  char out[LINK_VALUE_MAX];
  link_rounded(in, -1, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(62, strlen(out));
}

// ── link_take_value ─────────────────────────────────────────────────────────

static void test_take_value_keeps_the_last_good_value(void) {
  char v[LINK_VALUE_MAX] = "21.4";
  TEST_ASSERT_FALSE(link_take_value(v, sizeof(v), "", true));          // retraction
  TEST_ASSERT_FALSE(link_take_value(v, sizeof(v), nullptr, true));
  TEST_ASSERT_FALSE(link_take_value(v, sizeof(v), "21.9", false));     // controller copy, removed
  TEST_ASSERT_EQUAL_STRING("21.4", v);
  TEST_ASSERT_TRUE(link_take_value(v, sizeof(v), "22.0", true));
  TEST_ASSERT_EQUAL_STRING("22.0", v);
}

static void test_take_value_cuts_on_a_character_boundary(void) {
  char v[4];
  TEST_ASSERT_TRUE(link_take_value(v, sizeof(v), "ab\xC2\xB0", true));
  TEST_ASSERT_EQUAL_STRING("ab", v);
}

// ── retry backoff ────────────────────────────────────────────────────────────

static void test_retry_backs_off_to_the_cap(void) {
  uint32_t w = 0;
  const uint32_t expected[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
  for (int i = 0; i < 7; i++) {
    w = link_next_retry_ms(w, 1000);
    TEST_ASSERT_EQUAL_UINT32(expected[i], w);
  }
}

static void test_retry_never_faster_than_the_interval(void) {
  TEST_ASSERT_EQUAL_UINT32(45000, link_next_retry_ms(0, 45000));
  TEST_ASSERT_EQUAL_UINT32(45000, link_next_retry_ms(45000, 45000));
  TEST_ASSERT_EQUAL_UINT32(30000, link_next_retry_ms(4000000000u, 100));   // no overflow
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_utf8_fit_never_splits_a_character);
  RUN_TEST(test_utf8_copy);
  RUN_TEST(test_glob_literal_and_star);
  RUN_TEST(test_resolve_star_device);
  RUN_TEST(test_resolve_star_in_both);
  RUN_TEST(test_resolve_honors_the_device_pattern);
  RUN_TEST(test_resolve_two_nodes_on_one_device_are_ambiguous);
  RUN_TEST(test_parse_two_and_three_parts);
  RUN_TEST(test_parse_takes_only_len_bytes);
  RUN_TEST(test_parse_sanitizes_plain_parts);
  RUN_TEST(test_parse_patterns);
  RUN_TEST(test_parse_rejects_malformed_patterns);
  RUN_TEST(test_parse_rejects_bad_shapes);
  RUN_TEST(test_parse_checks_each_part_against_its_limit);
  RUN_TEST(test_status_text);
  RUN_TEST(test_rounded_numbers);
  RUN_TEST(test_rounded_plain_decimal_forms);
  RUN_TEST(test_rounded_passes_text_through);
  RUN_TEST(test_rounded_never_overflows_a_small_buffer);
  RUN_TEST(test_render_placeholders);
  RUN_TEST(test_render_stops_at_the_buffer);
  RUN_TEST(test_render_cuts_on_a_character_boundary);
  RUN_TEST(test_long_values_are_cut_on_a_character_boundary);
  RUN_TEST(test_take_value_keeps_the_last_good_value);
  RUN_TEST(test_take_value_cuts_on_a_character_boundary);
  RUN_TEST(test_retry_backs_off_to_the_cap);
  RUN_TEST(test_retry_never_faster_than_the_interval);
  return UNITY_END();
}
