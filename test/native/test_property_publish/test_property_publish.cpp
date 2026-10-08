// Host-side tests for Property's publish paths in the core, through a FakeTransport:
// the publish-on-change gate, the empty-string and retraction payloads, the queued path's
// in-flight accounting, and the /set subscription with its retries.

#include <unity.h>
#include <homie/Device.h>
#include <homie/homie_settable.h>
#include "../support/fake_clock.h"
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

// Records what Property::subscribe() registers. The ESP32 port defines the real one until
// the settable table moves into the core.
static int _registered = 0;
static char _registered_topic[HOMIE_TOPIC_MAX + 1];
void subscribe_for_callbacks(const char* topic, property_settable_callback_t cb, Property* instance) {
    (void)cb;
    (void)instance;
    _registered++;
    snprintf(_registered_topic, sizeof(_registered_topic), "%s", topic);
}

static FakeTransport t;
static Device dev;
static Node* node = nullptr;

void setUp(void) {
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    fake_clock_bind();
    g_clock.reset();
    _registered = 0;
    _registered_topic[0] = '\0';
    if (!node) {
        dev.init("Dev", "dev", "generic", &t);
        node = dev.addNode("n", "N", "generic");
    }
}
void tearDown(void) {}

static void add(Property& p, const char* id, const char* datatype, bool retained = true) {
    node->addProperty(&p, id, id, datatype, "", false, retained, "");
}

static void test_retained_value_is_published_once_until_it_changes(void) {
    static Property p;
    add(p, "level", "integer");
    p.setValue(5);

    TEST_ASSERT_TRUE(p.publish_value());
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::PUBLISH, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/level", t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("5", t.at(0).payload);
    TEST_ASSERT_TRUE(t.at(0).retained);
    TEST_ASSERT_EQUAL_INT(2, t.at(0).qos);

    TEST_ASSERT_TRUE(p.publish_value());   // unchanged: suppressed, still a success
    TEST_ASSERT_EQUAL_INT(1, t.count());

    p.publish();                           // whole-tree republish forces
    TEST_ASSERT_EQUAL_INT(2, t.count());

    p.setValue(6);
    TEST_ASSERT_TRUE(p.publish_value());
    TEST_ASSERT_EQUAL_INT(3, t.count());
    TEST_ASSERT_EQUAL_STRING("6", t.at(2).payload);
}

static void test_event_property_is_never_gated_and_uses_qos_0(void) {
    static Property p;
    add(p, "press", "string", false);
    p.setValue("x");

    p.publish_value();
    p.publish_value();

    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_FALSE(t.at(1).retained);
    TEST_ASSERT_EQUAL_INT(0, t.at(1).qos);
}

static void test_empty_string_is_one_nul_byte(void) {
    static Property p;
    add(p, "label", "string");
    p.setValue("");

    p.publish_value();

    TEST_ASSERT_EQUAL_INT(1, t.at(0).length);
    TEST_ASSERT_EQUAL_INT('\0', t.at(0).payload[0]);
}

static void test_no_value_publishes_nothing(void) {
    static Property p;
    add(p, "unset", "string");

    TEST_ASSERT_FALSE(p.publish_value());
    TEST_ASSERT_FALSE(p.publish_queued());
    TEST_ASSERT_EQUAL_INT(0, t.count());
}

static void test_clear_value_retracts_and_forgets_the_memo(void) {
    static Property p;
    add(p, "temp", "float");
    p.setValue("1.5");
    p.publish_value();

    p.clearValue();
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/temp", t.at(1).topic);
    TEST_ASSERT_EQUAL_INT(0, t.at(1).length);
    TEST_ASSERT_TRUE(t.at(1).retained);
    TEST_ASSERT_FALSE(p.publish_value());   // no value until the next set

    p.setValue("1.5");
    p.publish_value();                      // same value as before, but the broker has none
    TEST_ASSERT_EQUAL_INT(3, t.count());
}

static void test_queued_publish_is_not_gated_while_in_flight(void) {
    static Property p;
    add(p, "soc", "integer");
    p.setValue(80);

    TEST_ASSERT_TRUE(p.publish_queued());
    TEST_ASSERT_EQUAL_INT(1, t.count_of(FakeTransport::QUEUED));
    const FakeTransport::Record* q = t.nth(FakeTransport::QUEUED, 0);
    TEST_ASSERT_EQUAL_PTR(&p, q->source);
    TEST_ASSERT_EQUAL_STRING("80", q->payload);
    TEST_ASSERT_EQUAL_INT(2, q->qos);
    TEST_ASSERT_EQUAL_UINT32(1, p.queued_count());

    // Not sent yet: the memo is still empty, so the same value is queued again.
    TEST_ASSERT_TRUE(p.publish_queued());
    TEST_ASSERT_EQUAL_INT(2, t.count_of(FakeTransport::QUEUED));

    // The queue reports both as sent; now the gate holds.
    p.queued_publish_done("80", 2, true);
    p.queued_publish_done("80", 2, true);
    TEST_ASSERT_TRUE(p.publish_queued());
    TEST_ASSERT_EQUAL_INT(2, t.count_of(FakeTransport::QUEUED));
    TEST_ASSERT_EQUAL_STRING("80", p.last_published());
}

static void test_refused_queue_publish_is_not_counted(void) {
    static Property p;
    add(p, "volts", "float");
    p.setValue("12.0");
    t.queue_result = false;

    TEST_ASSERT_FALSE(p.publish_queued());
    TEST_ASSERT_EQUAL_UINT32(0, p.queued_count());
}

static void test_subscribe_registers_the_set_topic_at_qos_0(void) {
    static Property p;
    add(p, "relay", "boolean");

    p.subscribe();

    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::SUBSCRIBE, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/relay/set", t.at(0).topic);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);
    TEST_ASSERT_EQUAL_INT(1, _registered);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/relay/set", _registered_topic);
    TEST_ASSERT_EQUAL_INT(0, g_clock.sleeps);
}

static void test_subscribe_retries_twice_then_gives_up(void) {
    static Property p;
    add(p, "mode", "string");
    t.subscribe_result = false;

    p.subscribe();

    TEST_ASSERT_EQUAL_INT(3, t.count_of(FakeTransport::SUBSCRIBE));
    TEST_ASSERT_EQUAL_INT(2, g_clock.sleeps);
    TEST_ASSERT_EQUAL_UINT32(750, g_clock.slept_ms);
    TEST_ASSERT_EQUAL_INT(0, _registered);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "FAILED TO SUBSCRIBE TO PROPERTY SET TOPIC"));
}

static void test_subscribe_while_disconnected_does_nothing(void) {
    static Property p;
    add(p, "fan", "boolean");
    t.is_connected = false;

    p.subscribe();

    TEST_ASSERT_EQUAL_INT(0, t.count());
    TEST_ASSERT_EQUAL_INT(0, _registered);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_retained_value_is_published_once_until_it_changes);
    RUN_TEST(test_event_property_is_never_gated_and_uses_qos_0);
    RUN_TEST(test_empty_string_is_one_nul_byte);
    RUN_TEST(test_no_value_publishes_nothing);
    RUN_TEST(test_clear_value_retracts_and_forgets_the_memo);
    RUN_TEST(test_queued_publish_is_not_gated_while_in_flight);
    RUN_TEST(test_refused_queue_publish_is_not_counted);
    RUN_TEST(test_subscribe_registers_the_set_topic_at_qos_0);
    RUN_TEST(test_subscribe_retries_twice_then_gives_up);
    RUN_TEST(test_subscribe_while_disconnected_does_nothing);
    return UNITY_END();
}
