// Host-side tests for the controller in the core, through a FakeTransport and a fake
// clock: what it subscribes and when, how it builds a device from $description, and the
// /set it publishes.

#include <unity.h>
#include <homie/controller.h>
#include "../support/fake_clock.h"
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

static FakeTransport t;

// Hand one message to the controller the way the port's receive callback does.
static void deliver(const char* topic, const char* payload) {
    char topic_buf[HOMIE_TOPIC_MAX + 1];
    static char payload_buf[2048];
    snprintf(topic_buf, sizeof(topic_buf), "%s", topic);
    size_t n = strlen(payload);
    memcpy(payload_buf, payload, n);
    controller_mqtt_callback(topic_buf, (uint8_t*)payload_buf, (unsigned)n);
}

void setUp(void) {
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    fake_clock_bind();
    g_clock.reset();
    g_clock.now = 1000;
    controller_init(&t, "homie", false);
}
void tearDown(void) {}

static void test_discovery_subscribes_state_in_its_domain(void) {
    controller_setup_discovery();
    TEST_ASSERT_EQUAL_INT(0, t.count());   // scheduled, not made

    controller_loop();

    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::SUBSCRIBE, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/+/$state", t.at(0).topic);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);

    controller_loop();                     // nothing pending
    TEST_ASSERT_EQUAL_INT(1, t.count());
}

static void test_failed_subscribe_is_retried_after_the_hold(void) {
    t.subscribe_result = false;
    controller_setup_discovery();
    controller_loop();
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "(error -3); retrying"));

    t.subscribe_result = true;
    g_clock.now += 999;
    controller_loop();
    TEST_ASSERT_EQUAL_INT(1, t.count());   // still holding

    g_clock.now += 1;
    controller_loop();
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING("homie/5/+/$state", t.at(1).topic);
}

static void test_new_device_is_subscribed_then_built_from_its_description(void) {
    controller_setup_discovery();
    controller_loop();
    t.clear();

    deliver("homie/5/dev1/$state", "ready");
    controller_loop();

    TEST_ASSERT_NOT_NULL(controller_get_device("dev1"));
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_READY, controller_effective_state("dev1"));
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/$description", t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/+/+", t.at(1).topic);

    deliver("homie/5/dev1/$description",
            "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Plug\",\"type\":\"plug\","
            "\"nodes\":{\"sw\":{\"name\":\"Switch\",\"type\":\"switch\",\"properties\":"
            "{\"on\":{\"name\":\"On\",\"datatype\":\"boolean\",\"settable\":true}}}}}");
    deliver("homie/5/dev1/sw/on", "true");
    controller_loop();

    Property* on = controller_get_property("dev1", "sw", "on");
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_TRUE(on->settable());
    TEST_ASSERT_EQUAL_STRING("true", on->value());
    TEST_ASSERT_EQUAL_STRING("plug", controller_get_device("dev1")->type());
    TEST_ASSERT_EQUAL_INT(2, t.count());   // building the device published nothing
}

static void test_set_property_publishes_a_non_retained_set(void) {
    controller_setup_discovery();
    controller_loop();
    deliver("homie/5/dev1/$state", "ready");
    deliver("homie/5/dev1/$description",
            "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Plug\",\"type\":\"plug\","
            "\"nodes\":{\"sw\":{\"name\":\"Switch\",\"type\":\"switch\",\"properties\":"
            "{\"on\":{\"name\":\"On\",\"datatype\":\"boolean\",\"settable\":true},"
            "\"watts\":{\"name\":\"Power\",\"datatype\":\"float\"}}}}}");
    controller_loop();
    t.clear();

    TEST_ASSERT_TRUE(controller_set_property("dev1", "sw", "on", "false"));
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::PUBLISH, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/sw/on/set", t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("false", t.at(0).payload);
    TEST_ASSERT_FALSE(t.at(0).retained);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);

    TEST_ASSERT_FALSE(controller_set_property("dev1", "sw", "watts", "5"));   // not settable
    t.is_connected = false;
    TEST_ASSERT_FALSE(controller_set_property("dev1", "sw", "on", "true"));
    TEST_ASSERT_EQUAL_INT(1, t.count());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_discovery_subscribes_state_in_its_domain);
    RUN_TEST(test_failed_subscribe_is_retried_after_the_hold);
    RUN_TEST(test_new_device_is_subscribed_then_built_from_its_description);
    RUN_TEST(test_set_property_publishes_a_non_retained_set);
    return UNITY_END();
}
