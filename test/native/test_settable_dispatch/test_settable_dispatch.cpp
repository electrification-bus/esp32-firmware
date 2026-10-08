// Host-side tests for the core's settable table and /set dispatch (homie_settable.h),
// through a FakeTransport: registration, the validate -> store -> driver -> publish order,
// a driver's refusal putting the old value back, $target, and the two entity overloads.
//
// NodeEntity is the firmware's driver base class, which the core knows only as a pointer;
// the minimal one below, and the caller bound with the table, stand in for it.

#include <unity.h>
#include <homie/Device.h>
#include <homie/homie_settable.h>
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

class NodeEntity {
 public:
    virtual ~NodeEntity() {}
    bool accept = true;
    int property_calls = 0;
    int name_calls = 0;
    char last_id[HOMIE_PROPERTY_ID_MAX + 1] = {0};
    char last_value[64] = {0};
    Property* publishes = nullptr;   // a property the callback publishes itself

    bool on_property(Property* p) {
        property_calls++;
        snprintf(last_value, sizeof(last_value), "%s", p->value());
        if (publishes) publishes->publish_queued();
        return accept;
    }
    virtual bool entity_settable_callback(const char* property_id, const char* value) {
        name_calls++;
        snprintf(last_id, sizeof(last_id), "%s", property_id);
        snprintf(last_value, sizeof(last_value), "%s", value);
        return accept;
    }
};

static bool call_entity(NodeEntity* entity, entity_settable_callback_t cb, Property* instance,
                        const char* property_id, const char* value) {
    if (instance) return (entity->*cb)(instance);
    return entity->entity_settable_callback(property_id, value);
}

struct HandlerLog {
    bool accept = true;
    int calls = 0;
    char value[64] = {0};
};
static bool handler(void* ctx, const char* value) {
    HandlerLog* h = (HandlerLog*)ctx;
    h->calls++;
    snprintf(h->value, sizeof(h->value), "%s", value);
    return h->accept;
}

static FakeTransport t;
static subscribed_settable_property_t _table[8];
static Device dev;
static Node* node = nullptr;
static Property _props[16];
static int _num_props = 0;

void setUp(void) {
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    settable_table_bind(_table, 8, &t, call_entity);
    if (!node) {
        dev.init("Dev", "dev", "generic", &t);
        node = dev.addNode("n", "N", "generic");
    }
}
void tearDown(void) {}

// A settable property under homie/5/dev/n/<id>, subscribed (so in the table), with the
// transport's record cleared.
static Property* settable(const char* id, const char* datatype, const char* format = "") {
    Property* p = &_props[_num_props++];
    node->addProperty(p, id, id, datatype, "", true, true, format);
    p->subscribe();
    t.clear();
    return p;
}

static void set_topic(Property* p, char* buf, size_t len) { snprintf(buf, len, "%s/set", p->topic()); }

static void test_set_is_stored_then_published(void) {
    Property* p = settable("relay", "boolean");
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));
    TEST_ASSERT_TRUE(settable_is_registered(topic));

    TEST_ASSERT_TRUE(settable_dispatch(topic, "on"));

    TEST_ASSERT_EQUAL_STRING("true", p->value());
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::PUBLISH, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING(p->topic(), t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("true", t.at(0).payload);
    TEST_ASSERT_TRUE(t.at(0).retained);
    TEST_ASSERT_EQUAL_INT(2, t.at(0).qos);
}

static void test_invalid_payload_is_neither_stored_nor_published(void) {
    Property* p = settable("level", "integer", "0:10");
    p->setValue(3);
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));

    TEST_ASSERT_TRUE(settable_dispatch(topic, "11"));   // handled, but out of range

    TEST_ASSERT_EQUAL_STRING("3", p->value());
    TEST_ASSERT_EQUAL_INT(0, t.count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "invalid integer payload '11'"));
}

static void test_target_is_published_first_with_the_exact_payload(void) {
    Property* p = settable("dim", "float", "0:100:0.5");
    p->setSupportsTarget(true);
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));

    settable_dispatch(topic, "40.2");

    char target[HOMIE_TOPIC_MAX + 1];
    snprintf(target, sizeof(target), "%s/$target", p->topic());
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING(target, t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("40.2", t.at(0).payload);
    TEST_ASSERT_TRUE(t.at(0).retained);
    TEST_ASSERT_EQUAL_STRING(p->topic(), t.at(1).topic);
    TEST_ASSERT_EQUAL_STRING("40.000000", t.at(1).payload);   // stepped and coerced
}

static void test_refused_set_restores_the_value_and_publishes_nothing(void) {
    Property* p = settable("mode", "enum", "off,low,high");
    p->setValue("low");
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));
    HandlerLog h;
    h.accept = false;
    subscribe_settable_handler(topic, handler, &h);
    NodeEntity e;
    entity_subscribe_for_callbacks(topic, &NodeEntity::on_property, &e);
    t.clear();

    TEST_ASSERT_TRUE(settable_dispatch(topic, "high"));

    TEST_ASSERT_EQUAL_INT(1, h.calls);
    TEST_ASSERT_EQUAL_STRING("high", h.value);
    TEST_ASSERT_EQUAL_INT(0, e.property_calls);   // a refusing handler stops the chain
    TEST_ASSERT_EQUAL_STRING("low", p->value());
    TEST_ASSERT_EQUAL_INT(0, t.count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "refused by its driver, not published"));
}

static void test_entity_gets_the_property_overload_when_a_property_exists(void) {
    Property* p = settable("power", "boolean");
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));
    NodeEntity e;
    entity_subscribe_for_callbacks(topic, &NodeEntity::on_property, &e);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "SUCCESS"));
    TEST_ASSERT_EQUAL_INT(0, t.count());   // already subscribed by the property

    settable_dispatch(topic, "true");

    TEST_ASSERT_EQUAL_INT(1, e.property_calls);
    TEST_ASSERT_EQUAL_INT(0, e.name_calls);
    TEST_ASSERT_EQUAL_STRING("true", e.last_value);   // stored before the driver runs
    TEST_ASSERT_EQUAL_INT(1, t.count());
}

static void test_entity_gets_the_name_overload_without_a_property(void) {
    NodeEntity e;
    entity_subscribe_for_callbacks("homie/5/dev/n/fan-speed/set", &NodeEntity::on_property, &e);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "CREATED NEW"));
    TEST_ASSERT_EQUAL_INT(1, t.count());   // connected: subscribed at once, QoS 0
    TEST_ASSERT_EQUAL_INT(FakeTransport::SUBSCRIBE, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/fan-speed/set", t.at(0).topic);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);
    t.clear();

    TEST_ASSERT_TRUE(settable_dispatch("homie/5/dev/n/fan-speed/set", "3"));

    TEST_ASSERT_EQUAL_INT(1, e.name_calls);
    TEST_ASSERT_EQUAL_STRING("fan-speed", e.last_id);
    TEST_ASSERT_EQUAL_STRING("3", e.last_value);
    TEST_ASSERT_EQUAL_INT(0, t.count());   // no Property, so nothing to publish
}

static void test_entity_registered_while_disconnected_is_not_subscribed_yet(void) {
    NodeEntity e;
    t.is_connected = false;

    entity_subscribe_for_callbacks("homie/5/dev/n/valve/set", &NodeEntity::on_property, &e);

    TEST_ASSERT_EQUAL_INT(0, t.count());
    TEST_ASSERT_EQUAL_INT(1, settable_count());
    TEST_ASSERT_EQUAL_STRING("homie/5/dev/n/valve/set", settable_topic(0));
}

static void test_driver_that_publishes_itself_suppresses_the_value_echo(void) {
    Property* p = settable("heat", "boolean");
    p->setSupportsTarget(true);
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));
    NodeEntity e;
    e.publishes = p;
    entity_subscribe_for_callbacks(topic, &NodeEntity::on_property, &e);
    t.clear();

    settable_dispatch(topic, "true");

    TEST_ASSERT_EQUAL_INT(1, t.count_of(FakeTransport::QUEUED));
    TEST_ASSERT_EQUAL_INT(1, t.count_of(FakeTransport::PUBLISH));   // $target only
    TEST_ASSERT_NOT_NULL(strstr(t.nth(FakeTransport::PUBLISH, 0)->topic, "/$target"));
}

static void test_local_set_takes_the_same_path(void) {
    Property* p = settable("light", "boolean");
    char topic[HOMIE_TOPIC_MAX + 1];
    set_topic(p, topic, sizeof(topic));

    TEST_ASSERT_TRUE(deliver_local_set(topic, "1"));
    TEST_ASSERT_EQUAL_STRING("true", p->value());
    TEST_ASSERT_EQUAL_INT(1, t.count());

    TEST_ASSERT_FALSE(deliver_local_set("homie/5/dev/n/missing/set", "1"));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "local set NOT DELIVERED"));
}

static void test_registration_updates_in_place_and_refuses_when_full(void) {
    static subscribed_settable_property_t small[2];
    settable_table_bind(small, 2, &t, call_entity);
    HandlerLog a, b;
    subscribe_settable_handler("homie/5/dev/x/a/set", handler, &a);
    subscribe_settable_handler("homie/5/dev/x/a/set", handler, &b);   // same topic: replaced
    TEST_ASSERT_EQUAL_INT(1, settable_count());
    settable_dispatch("homie/5/dev/x/a/set", "v");
    TEST_ASSERT_EQUAL_INT(0, a.calls);
    TEST_ASSERT_EQUAL_INT(1, b.calls);

    subscribe_settable_handler("homie/5/dev/x/b/set", handler, &a);
    g_log.clear();
    subscribe_settable_handler("homie/5/dev/x/c/set", handler, &a);
    TEST_ASSERT_EQUAL_INT(2, settable_count());
    TEST_ASSERT_EQUAL_INT(1, g_log.errors);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last_error, "settable subscriber table full (2)"));
    TEST_ASSERT_FALSE(settable_is_registered("homie/5/dev/x/c/set"));
}

static void test_over_long_topic_is_not_registered(void) {
    char topic[HOMIE_TOPIC_MAX + 2];
    memset(topic, 'a', sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';
    HandlerLog h;

    subscribe_settable_handler(topic, handler, &h);

    TEST_ASSERT_EQUAL_INT(0, settable_count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "not registered"));
}

static void test_unbound_table_and_missing_caller_are_reported(void) {
    settable_table_bind(nullptr, 0, &t, call_entity);
    HandlerLog h;
    subscribe_settable_handler("homie/5/dev/x/a/set", handler, &h);
    TEST_ASSERT_EQUAL_INT(0, settable_count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last_error, "table not set up"));

    settable_table_bind(_table, 8, &t, nullptr);
    NodeEntity e;
    entity_subscribe_for_callbacks("homie/5/dev/x/b/set", &NodeEntity::on_property, &e);
    settable_dispatch("homie/5/dev/x/b/set", "v");
    TEST_ASSERT_EQUAL_INT(0, e.name_calls);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last_error, "no entity caller bound"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_set_is_stored_then_published);
    RUN_TEST(test_invalid_payload_is_neither_stored_nor_published);
    RUN_TEST(test_target_is_published_first_with_the_exact_payload);
    RUN_TEST(test_refused_set_restores_the_value_and_publishes_nothing);
    RUN_TEST(test_entity_gets_the_property_overload_when_a_property_exists);
    RUN_TEST(test_entity_gets_the_name_overload_without_a_property);
    RUN_TEST(test_entity_registered_while_disconnected_is_not_subscribed_yet);
    RUN_TEST(test_driver_that_publishes_itself_suppresses_the_value_echo);
    RUN_TEST(test_local_set_takes_the_same_path);
    RUN_TEST(test_registration_updates_in_place_and_refuses_when_full);
    RUN_TEST(test_over_long_topic_is_not_registered);
    RUN_TEST(test_unbound_table_and_missing_caller_are_reported);
    return UNITY_END();
}
