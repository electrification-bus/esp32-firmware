// Host-side tests for lib/ebus_core/src/homie/controller_inbox.cpp: the inbox that
// carries controller messages out of the MQTT receive callback, and the controller's
// topic parser.

#include <unity.h>
#include <homie/controller_inbox.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// Bytes one message takes in the arena: 4-byte header, topic + NUL, payload + NUL.
static size_t rec(const char* topic, const char* payload) {
  return 4 + strlen(topic) + 1 + strlen(payload) + 1;
}

static bool push(ControllerInbox& in, const char* topic, const char* payload) {
  return in.push(topic, (const uint8_t*)payload, strlen(payload));
}

static void expect_front(ControllerInbox& in, const char* topic, const char* payload) {
  const char* t;
  const char* p;
  size_t len;
  TEST_ASSERT_TRUE(in.front(&t, &p, &len));
  TEST_ASSERT_EQUAL_STRING(topic, t);
  TEST_ASSERT_EQUAL_STRING(payload, p);
  TEST_ASSERT_EQUAL_UINT32(strlen(payload), len);
}

// --- inbox ---------------------------------------------------------------------------

static void test_inbox_is_fifo(void) {
  uint8_t arena[256];
  ControllerInbox in(arena, sizeof(arena));
  TEST_ASSERT_FALSE(in.front(nullptr, nullptr, nullptr));
  TEST_ASSERT_TRUE(push(in, "ebus/5/a/$state", "init"));
  TEST_ASSERT_TRUE(push(in, "ebus/5/a/$description", "{}"));
  TEST_ASSERT_TRUE(push(in, "ebus/5/a/n/p", "1"));
  TEST_ASSERT_EQUAL_UINT32(3, in.count());
  expect_front(in, "ebus/5/a/$state", "init");
  in.pop();
  expect_front(in, "ebus/5/a/$description", "{}");
  in.pop();
  expect_front(in, "ebus/5/a/n/p", "1");
  in.pop();
  TEST_ASSERT_EQUAL_UINT32(0, in.count());
  TEST_ASSERT_EQUAL_UINT32(0, in.used());
}

static void test_inbox_terminates_an_unterminated_payload(void) {
  uint8_t arena[64];
  ControllerInbox in(arena, sizeof(arena));
  const char bytes[] = {'r', 'e', 'a', 'd', 'y', 'X', 'X'};
  TEST_ASSERT_TRUE(in.push("t", (const uint8_t*)bytes, 5));
  expect_front(in, "t", "ready");
}

static void test_inbox_keeps_an_empty_payload(void) {
  uint8_t arena[64];
  ControllerInbox in(arena, sizeof(arena));
  TEST_ASSERT_TRUE(in.push("t", nullptr, 0));
  expect_front(in, "t", "");
}

static void test_inbox_refuses_what_does_not_fit_and_keeps_the_rest(void) {
  uint8_t arena[40];
  ControllerInbox in(arena, sizeof(arena));
  TEST_ASSERT_TRUE(push(in, "a", "0123456789"));      // 17 bytes
  TEST_ASSERT_TRUE(push(in, "b", "0123456789"));      // 34
  TEST_ASSERT_FALSE(push(in, "c", "0123456789"));     // would be 51
  TEST_ASSERT_EQUAL_UINT32(2, in.count());
  TEST_ASSERT_EQUAL_UINT32(34, in.used());
  expect_front(in, "a", "0123456789");
}

static void test_inbox_refuses_a_message_larger_than_the_arena(void) {
  uint8_t arena[16];
  ControllerInbox in(arena, sizeof(arena));
  TEST_ASSERT_FALSE(push(in, "topic", "a payload too long for it"));
  TEST_ASSERT_EQUAL_UINT32(0, in.count());
}

static void test_inbox_wraps_and_stays_in_order(void) {
  uint8_t arena[48];
  ControllerInbox in(arena, sizeof(arena));
  TEST_ASSERT_EQUAL_UINT32(17, rec("a", "0123456789"));
  TEST_ASSERT_TRUE(push(in, "a", "0123456789"));      // [0, 17)
  TEST_ASSERT_TRUE(push(in, "b", "0123456789"));      // [17, 34)
  in.pop();                                          // frees [0, 17)
  // 14 bytes left at the end, so this one wraps to offset 0.
  TEST_ASSERT_TRUE(push(in, "c", "0123456789"));
  // The gap is now [17, 17): full.
  TEST_ASSERT_FALSE(push(in, "d", ""));
  expect_front(in, "b", "0123456789");
  in.pop();
  expect_front(in, "c", "0123456789");
  // With "c" alone at [0, 17), the space after it is free again.
  TEST_ASSERT_TRUE(push(in, "d", "x"));
  in.pop();
  expect_front(in, "d", "x");
  in.pop();
  TEST_ASSERT_EQUAL_UINT32(0, in.count());
}

static void test_inbox_cycles_many_times(void) {
  uint8_t arena[100];
  ControllerInbox in(arena, sizeof(arena));
  char topic[16];
  char payload[16];
  int pushed = 0;
  int popped = 0;
  for (int round = 0; round < 500; round++) {
    // Push until full, then pop about half, so the write position walks round the arena.
    for (;;) {
      snprintf(topic, sizeof(topic), "t%d", pushed);
      snprintf(payload, sizeof(payload), "%d", pushed * 7);
      if (!push(in, topic, payload)) break;
      pushed++;
    }
    size_t drop = (in.count() + 1) / 2;
    for (size_t i = 0; i < drop; i++) {
      snprintf(topic, sizeof(topic), "t%d", popped);
      snprintf(payload, sizeof(payload), "%d", popped * 7);
      expect_front(in, topic, payload);
      in.pop();
      popped++;
    }
  }
  TEST_ASSERT_TRUE(pushed > 1000);
}

static void test_inbox_clear(void) {
  uint8_t arena[64];
  ControllerInbox in(arena, sizeof(arena));
  push(in, "a", "1");
  push(in, "b", "2");
  in.clear();
  TEST_ASSERT_EQUAL_UINT32(0, in.count());
  TEST_ASSERT_EQUAL_UINT32(0, in.used());
  TEST_ASSERT_FALSE(in.front(nullptr, nullptr, nullptr));
}

// --- topic parser --------------------------------------------------------------------

// The ids from the hardware run: a 33-char child id was cut to 31 before.
static const char* CHILD_ID = "ableedge-proxy-1-0000ha2603300003";

static void test_parse_state(void) {
  ControllerTopic t;
  TEST_ASSERT_TRUE(controller_parse_topic("ebus/5/b0b21c90f570/$state", &t));
  TEST_ASSERT_EQUAL_INT(CONTROLLER_TOPIC_STATE, t.kind);
  TEST_ASSERT_EQUAL_STRING("ebus", t.domain);
  TEST_ASSERT_EQUAL_STRING("b0b21c90f570", t.device_id);
  TEST_ASSERT_EQUAL_STRING("", t.node_id);
  TEST_ASSERT_EQUAL_STRING("", t.property_id);
}

static void test_parse_description_keeps_a_long_child_id(void) {
  char topic[128];
  snprintf(topic, sizeof(topic), "ebus/5/%s/$description", CHILD_ID);
  ControllerTopic t;
  TEST_ASSERT_TRUE(controller_parse_topic(topic, &t));
  TEST_ASSERT_EQUAL_INT(CONTROLLER_TOPIC_DESCRIPTION, t.kind);
  TEST_ASSERT_EQUAL_STRING(CHILD_ID, t.device_id);
}

static void test_parse_property(void) {
  char topic[128];
  snprintf(topic, sizeof(topic), "homie/5/%s/meter/active-power", CHILD_ID);
  ControllerTopic t;
  TEST_ASSERT_TRUE(controller_parse_topic(topic, &t));
  TEST_ASSERT_EQUAL_INT(CONTROLLER_TOPIC_PROPERTY, t.kind);
  TEST_ASSERT_EQUAL_STRING("homie", t.domain);
  TEST_ASSERT_EQUAL_STRING(CHILD_ID, t.device_id);
  TEST_ASSERT_EQUAL_STRING("meter", t.node_id);
  TEST_ASSERT_EQUAL_STRING("active-power", t.property_id);
}

static void test_parse_ignores_other_topics(void) {
  ControllerTopic t;
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/$log", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/$alert/battery", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/node/$target", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/node/prop/set", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/node/prop/$target", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5//$state", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus//dev/$state", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("ebus/5/dev/node/", &t));
  TEST_ASSERT_FALSE(controller_parse_topic("", &t));
  TEST_ASSERT_FALSE(controller_parse_topic(nullptr, &t));
}

static void test_parse_refuses_ids_over_the_limits(void) {
  char id[HOMIE_DEVICE_ID_MAX + 2];
  memset(id, 'd', sizeof(id) - 1);
  id[sizeof(id) - 1] = '\0';                       // HOMIE_DEVICE_ID_MAX + 1 chars
  char topic[256];
  ControllerTopic t;

  snprintf(topic, sizeof(topic), "ebus/5/%s/$state", id);
  TEST_ASSERT_FALSE(controller_parse_topic(topic, &t));
  id[HOMIE_DEVICE_ID_MAX] = '\0';                  // exactly the limit
  snprintf(topic, sizeof(topic), "ebus/5/%s/$state", id);
  TEST_ASSERT_TRUE(controller_parse_topic(topic, &t));
  TEST_ASSERT_EQUAL_UINT32(HOMIE_DEVICE_ID_MAX, strlen(t.device_id));

  char node[HOMIE_NODE_ID_MAX + 2];
  memset(node, 'n', sizeof(node) - 1);
  node[sizeof(node) - 1] = '\0';
  snprintf(topic, sizeof(topic), "ebus/5/dev/%s/p", node);
  TEST_ASSERT_FALSE(controller_parse_topic(topic, &t));

  char prop[HOMIE_PROPERTY_ID_MAX + 2];
  memset(prop, 'p', sizeof(prop) - 1);
  prop[sizeof(prop) - 1] = '\0';
  snprintf(topic, sizeof(topic), "ebus/5/dev/n/%s", prop);
  TEST_ASSERT_FALSE(controller_parse_topic(topic, &t));

  TEST_ASSERT_FALSE(controller_parse_topic("averyverylongdomain/5/dev/$state", &t));
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_inbox_is_fifo);
  RUN_TEST(test_inbox_terminates_an_unterminated_payload);
  RUN_TEST(test_inbox_keeps_an_empty_payload);
  RUN_TEST(test_inbox_refuses_what_does_not_fit_and_keeps_the_rest);
  RUN_TEST(test_inbox_refuses_a_message_larger_than_the_arena);
  RUN_TEST(test_inbox_wraps_and_stays_in_order);
  RUN_TEST(test_inbox_cycles_many_times);
  RUN_TEST(test_inbox_clear);

  RUN_TEST(test_parse_state);
  RUN_TEST(test_parse_description_keeps_a_long_child_id);
  RUN_TEST(test_parse_property);
  RUN_TEST(test_parse_ignores_other_topics);
  RUN_TEST(test_parse_refuses_ids_over_the_limits);

  return UNITY_END();
}
