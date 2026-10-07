// Host-side tests for src/platform/broker_discovery.cpp. The service names come from the
// eBus specification, framework.md "Broker Discovery".

#include <unity.h>
#include <platform/broker_discovery.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// --- service table -----------------------------------------------------------------

static void test_parse_knows_the_four_service_names(void) {
    TEST_ASSERT_EQUAL_INT(BROKER_SVC_SECURE_MQTT, broker_service_parse("secure-mqtt"));
    TEST_ASSERT_EQUAL_INT(BROKER_SVC_MQTT, broker_service_parse("mqtt"));
    TEST_ASSERT_EQUAL_INT(BROKER_SVC_MQTT_WS, broker_service_parse("mqtt-ws"));
    TEST_ASSERT_EQUAL_INT(BROKER_SVC_MQTT_WSS, broker_service_parse("mqtt-wss"));
}

static void test_parse_rejects_other_spellings(void) {
    TEST_ASSERT_EQUAL_INT(-1, broker_service_parse("_mqtt._tcp"));
    TEST_ASSERT_EQUAL_INT(-1, broker_service_parse("MQTT"));
    TEST_ASSERT_EQUAL_INT(-1, broker_service_parse(""));
    TEST_ASSERT_EQUAL_INT(-1, broker_service_parse(nullptr));
}

static void test_default_ports(void) {
    TEST_ASSERT_EQUAL_UINT16(8883, broker_service_default_port(BROKER_SVC_SECURE_MQTT));
    TEST_ASSERT_EQUAL_UINT16(1883, broker_service_default_port(BROKER_SVC_MQTT));
    TEST_ASSERT_EQUAL_UINT16(9001, broker_service_default_port(BROKER_SVC_MQTT_WS));
    TEST_ASSERT_EQUAL_UINT16(9002, broker_service_default_port(BROKER_SVC_MQTT_WSS));
    TEST_ASSERT_EQUAL_UINT16(0, broker_service_default_port(BROKER_SVC_COUNT));
}

static void test_only_tcp_and_tls_are_supported(void) {
    TEST_ASSERT_TRUE(broker_service_supported(BROKER_SVC_SECURE_MQTT));
    TEST_ASSERT_TRUE(broker_service_supported(BROKER_SVC_MQTT));
    TEST_ASSERT_FALSE(broker_service_supported(BROKER_SVC_MQTT_WS));
    TEST_ASSERT_FALSE(broker_service_supported(BROKER_SVC_MQTT_WSS));
    TEST_ASSERT_FALSE(broker_service_supported(BROKER_SVC_COUNT));
    TEST_ASSERT_EQUAL_STRING("", broker_service_name(BROKER_SVC_COUNT));
}

// --- discovery list ----------------------------------------------------------------

static void test_default_is_secure_mqtt_then_mqtt(void) {
    BrokerDiscoveryList list;
    broker_discovery_default(&list);
    TEST_ASSERT_EQUAL_UINT8(2, list.count);
    TEST_ASSERT_EQUAL_STRING("secure-mqtt", broker_service_name(list.services[0]));
    TEST_ASSERT_EQUAL_STRING("mqtt", broker_service_name(list.services[1]));
}

static void test_add_keeps_the_given_order(void) {
    BrokerDiscoveryList list;
    broker_discovery_clear(&list);
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_OK, broker_discovery_add(&list, "mqtt"));
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_OK, broker_discovery_add(&list, "secure-mqtt"));
    TEST_ASSERT_EQUAL_UINT8(2, list.count);
    TEST_ASSERT_EQUAL_UINT8(BROKER_SVC_MQTT, list.services[0]);
    TEST_ASSERT_EQUAL_UINT8(BROKER_SVC_SECURE_MQTT, list.services[1]);
}

static void test_add_rejects_unknown_unsupported_and_duplicate(void) {
    BrokerDiscoveryList list;
    broker_discovery_clear(&list);
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_OK, broker_discovery_add(&list, "secure-mqtt"));
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_UNKNOWN, broker_discovery_add(&list, "amqp"));
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_UNSUPPORTED, broker_discovery_add(&list, "mqtt-ws"));
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_UNSUPPORTED, broker_discovery_add(&list, "mqtt-wss"));
    TEST_ASSERT_EQUAL_INT(BROKER_ADD_DUPLICATE, broker_discovery_add(&list, "secure-mqtt"));
    TEST_ASSERT_EQUAL_UINT8(1, list.count);
}

// --- host name -----------------------------------------------------------------------

static void test_hostname_drops_local_suffix(void) {
    char out[32];
    TEST_ASSERT_TRUE(broker_copy_hostname("span-nt-2025.local", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("span-nt-2025", out);
    TEST_ASSERT_TRUE(broker_copy_hostname("span-nt-2025.local.", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("span-nt-2025", out);
}

static void test_hostname_bare_name_is_kept(void) {
    char out[32];
    TEST_ASSERT_TRUE(broker_copy_hostname("broker", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("broker", out);
}

static void test_hostname_empty_or_too_long_is_refused(void) {
    char out[8];
    TEST_ASSERT_FALSE(broker_copy_hostname("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(broker_copy_hostname(".local", out, sizeof(out)));
    TEST_ASSERT_FALSE(broker_copy_hostname(nullptr, out, sizeof(out)));
    TEST_ASSERT_FALSE(broker_copy_hostname("12345678", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_TRUE(broker_copy_hostname("1234567.local", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("1234567", out);
}

// --- re-resolve decision ---------------------------------------------------------------

static void test_reresolve_on_every_third_consecutive_failure(void) {
    BrokerReconnect state = {0};
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_TRUE(broker_reconnect_failed(&state));
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_TRUE(broker_reconnect_failed(&state));
}

static void test_success_restarts_the_count(void) {
    BrokerReconnect state = {0};
    broker_reconnect_failed(&state);
    broker_reconnect_failed(&state);
    broker_reconnect_succeeded(&state);
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_FALSE(broker_reconnect_failed(&state));
    TEST_ASSERT_TRUE(broker_reconnect_failed(&state));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_knows_the_four_service_names);
    RUN_TEST(test_parse_rejects_other_spellings);
    RUN_TEST(test_default_ports);
    RUN_TEST(test_only_tcp_and_tls_are_supported);

    RUN_TEST(test_default_is_secure_mqtt_then_mqtt);
    RUN_TEST(test_add_keeps_the_given_order);
    RUN_TEST(test_add_rejects_unknown_unsupported_and_duplicate);

    RUN_TEST(test_hostname_drops_local_suffix);
    RUN_TEST(test_hostname_bare_name_is_kept);
    RUN_TEST(test_hostname_empty_or_too_long_is_refused);

    RUN_TEST(test_reresolve_on_every_third_consecutive_failure);
    RUN_TEST(test_success_restarts_the_count);

    return UNITY_END();
}
