// Host-side tests for the Homie 5 device lifecycle in src/homie/Device.cpp: the order in
// which a device tree puts $state, $description and values on the wire. The rules come
// from the Homie 5 convention: "$description ... may only change when the device $state
// is either init, disconnected, or lost", and "Adding children" (publish the child
// init -> details -> ready, then the parent init -> description with the child -> ready).
//
// Device.cpp is compiled into this suite directly, against the Arduino.h and MQTT.h
// stand-ins next to this file, and publishes through a FakeTransport that records every
// publish. Node, Property and the JSON helpers it links against are stubbed below: a Node
// publishes one value topic.

#include <unity.h>
#include "../../../src/homie/Device.cpp"
#include "../support/fake_transport.h"

// ---- stubs for what Device.cpp links against ------------------------------------------

uint8_t mqtt_qos = 2;

bool jsonExists(JsonVariant variant) { return !variant.isNull(); }
void property_from_dict(Property& property, JsonObject* props_obj) { (void)property; (void)props_obj; }
void node_serialize_into(Node& node, JsonObject& obj) { obj[HOMIE_NAME] = node.name(); }

Property::Property() {}
void Property::setNode(Node* node) { _parent_node = node; }
void Property::setMQTTClient(HomieTransport* transport) { _transport = transport; }
bool Property::settable() { return false; }
void Property::subscribe() {}
void Property::setSupportsTarget(bool t) { _supports_target = t; }

Node::Node() : _transport(nullptr), _device(nullptr) {}
Node::~Node() {}
void Node::addProperty(Property* property) { (void)property; }
void Node::addProperty(Property* property, const char* id, const char* name, const char* datatype,
                       const char* unit, bool settable, bool retained, const char* format) {
    (void)property; (void)id; (void)name; (void)datatype;
    (void)unit; (void)settable; (void)retained; (void)format;
}
void Node::setId(const char* id) { snprintf(_id, sizeof(_id), "%s", id); }
const char* Node::id() { return _id; }
void Node::setName(const char* name) { snprintf(_name, sizeof(_name), "%s", name); }
const char* Node::name() { return _name; }
void Node::setType(const char* type) { snprintf(_type, sizeof(_type), "%s", type); }
void Node::setDevice(Device* device) { _device = device; }
void Node::setMQTTClient(HomieTransport* transport) { _transport = transport; }
void Node::mqttConnected() {}
void Node::setTopic(const char* topic) { snprintf(_topic, sizeof(_topic), "%s", topic); }
void Node::clearRetained() {}
void Node::publish() {
    char top[128];
    snprintf(top, sizeof(top), "%s%s/value", _topic, _id);
    _transport->publish(top, "v", true, 2);
}

// ---- helpers ----------------------------------------------------------------------------

static FakeTransport client;

void setUp(void) { client.clear(); }
void tearDown(void) {}

static void expect(int i, const char* topic, const char* payload) {
    TEST_ASSERT_TRUE_MESSAGE(i < client.count(), topic);
    TEST_ASSERT_EQUAL_STRING(topic, client.at(i).topic);
    if (payload) TEST_ASSERT_EQUAL_STRING(payload, client.at(i).payload);
}

static bool ends_with(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Index of the first record on `topic` with `payload` (any payload if null), or -1.
static int find(const char* topic, const char* payload) {
    for (int i = 0; i < client.count(); i++) {
        if (strcmp(client.at(i).topic, topic) != 0) continue;
        if (!payload || strcmp(client.at(i).payload, payload) == 0) return i;
    }
    return -1;
}

static int find_attr(Device& d, const char* attr, const char* payload) {
    char top[160];
    snprintf(top, sizeof(top), "%s%s", d.topic(), attr);
    return find(top, payload);
}

// Every $description in the log went out while its own device's last $state was init.
static void assert_descriptions_only_while_init(void) {
    for (int i = 0; i < client.count(); i++) {
        const char* t = client.at(i).topic;
        if (!ends_with(t, "/$description")) continue;
        size_t prefix = strlen(t) - strlen("$description");
        const char* last_state = nullptr;
        for (int j = 0; j < i; j++) {
            const char* s = client.at(j).topic;
            if (ends_with(s, "/$state") && strlen(s) == prefix + strlen("$state") &&
                strncmp(s, t, prefix) == 0) {
                last_state = client.at(j).payload;
            }
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(last_state, t);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("init", last_state, t);
    }
}

// ---- tests ------------------------------------------------------------------------------

static void test_boot_publishes_child_fully_before_parent(void) {
    Device root;
    Device relay;
    root.init("Root", "root", "generic", &client);
    root.addNode("status", "Status", "generic");
    relay.init("Relay", "root-relay", "generic", root.mqttClient());
    root.addChild(&relay);
    relay.addNode("led", "LED", "generic");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, client.count(), "building the tree must not publish");

    root.publishTree();

    expect(0, "homie/5/root-relay/$state", "init");
    expect(1, "homie/5/root-relay/$description", nullptr);
    expect(2, "homie/5/root-relay/led/value", "v");
    expect(3, "homie/5/root-relay/$state", "ready");
    expect(4, "homie/5/root/$state", "init");
    expect(5, "homie/5/root/$description", nullptr);
    expect(6, "homie/5/root/status/value", "v");
    expect(7, "homie/5/root/$state", "ready");
    TEST_ASSERT_EQUAL_INT(8, client.count());
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_READY, root.state());
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_READY, relay.state());
}

static void test_boot_orders_a_deeper_tree(void) {
    Device root, a, b, a1;
    root.init("Root", "root", "generic", &client);
    a.init("A", "root-a", "generic", &client);
    b.init("B", "root-b", "generic", &client);
    a1.init("A1", "root-a-a1", "generic", &client);
    root.addChild(&a);
    root.addChild(&b);
    a.addChild(&a1);

    root.publishTree();

    assert_descriptions_only_while_init();
    Device* all[] = {&root, &a, &b, &a1};
    for (Device* d : all) {
        int init = find_attr(*d, "$state", "init");
        int desc = find_attr(*d, "$description", nullptr);
        int ready = find_attr(*d, "$state", "ready");
        TEST_ASSERT_TRUE_MESSAGE(init >= 0 && init < desc && desc < ready, d->getId());
        TEST_ASSERT_EQUAL_INT_MESSAGE(DEVICE_STATE_READY, d->state(), d->getId());
    }
    // A parent's $description, which lists its children, follows each child's ready.
    TEST_ASSERT_TRUE(find_attr(a1, "$state", "ready") < find_attr(a, "$description", nullptr));
    TEST_ASSERT_TRUE(find_attr(a, "$state", "ready") < find_attr(root, "$description", nullptr));
    TEST_ASSERT_TRUE(find_attr(b, "$state", "ready") < find_attr(root, "$description", nullptr));
    // The root leaves "lost" (the Last Will) last, and only once.
    expect(client.count() - 1, "homie/5/root/$state", "ready");
    int root_inits = 0;
    for (int i = 0; i < client.count(); i++) {
        if (strcmp(client.at(i).topic, "homie/5/root/$state") == 0 &&
            strcmp(client.at(i).payload, "init") == 0) {
            root_inits++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, root_inits);
}

static void test_reconnect_republishes_state_only(void) {
    Device root, relay;
    root.init("Root", "root", "generic", &client);
    relay.init("Relay", "root-relay", "generic", &client);
    root.addChild(&relay);
    root.publishTree();
    client.clear();

    root.publishStateTree();

    expect(0, "homie/5/root/$state", "ready");
    expect(1, "homie/5/root-relay/$state", "ready");
    TEST_ASSERT_EQUAL_INT(2, client.count());
}

static void test_live_add_child_follows_adding_children_order(void) {
    Device root, kid;
    root.init("Root", "root", "generic", &client);
    root.publishTree();
    client.clear();
    kid.init("Kid", "root-kid", "generic", &client);

    root.addChildLive(&kid);

    expect(0, "homie/5/root-kid/$state", "init");
    expect(1, "homie/5/root-kid/$description", nullptr);
    expect(2, "homie/5/root-kid/$state", "ready");
    expect(3, "homie/5/root/$state", "init");
    expect(4, "homie/5/root/$description", nullptr);
    expect(5, "homie/5/root/$state", "ready");
    TEST_ASSERT_EQUAL_INT(6, client.count());
}

static void test_over_long_id_is_truncated_and_logged_once(void) {
    char id[HOMIE_DEVICE_ID_MAX + 11];
    memset(id, 'a', sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    char want_id[HOMIE_DEVICE_ID_MAX + 1];
    memcpy(want_id, id, HOMIE_DEVICE_ID_MAX);
    want_id[HOMIE_DEVICE_ID_MAX] = '\0';
    char want_topic[HOMIE_DEVICE_TOPIC_MAX + 1];
    snprintf(want_topic, sizeof(want_topic), "homie/5/%s/", want_id);
    Serial.errors = 0;

    Device d;
    d.init("Long", id, "generic", &client);

    TEST_ASSERT_EQUAL_STRING(want_id, d.getId());
    TEST_ASSERT_EQUAL_STRING(want_topic, d.topic());
    TEST_ASSERT_EQUAL_INT(1, Serial.errors);
    TEST_ASSERT_NOT_NULL(strstr(Serial.last_error, "device id"));
}

static void test_id_at_the_limit_is_not_logged(void) {
    char id[HOMIE_DEVICE_ID_MAX + 1];
    memset(id, 'b', sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    Serial.errors = 0;

    Device d;
    d.init("Max", id, "generic", &client);

    TEST_ASSERT_EQUAL_STRING(id, d.getId());
    TEST_ASSERT_EQUAL_INT(0, Serial.errors);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_boot_publishes_child_fully_before_parent);
    RUN_TEST(test_boot_orders_a_deeper_tree);
    RUN_TEST(test_reconnect_republishes_state_only);
    RUN_TEST(test_live_add_child_follows_adding_children_order);
    RUN_TEST(test_over_long_id_is_truncated_and_logged_once);
    RUN_TEST(test_id_at_the_limit_is_not_logged);
    return UNITY_END();
}
