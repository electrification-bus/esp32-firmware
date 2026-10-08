// SPDX-License-Identifier: MIT
// ebus-posix-device: a hand-coded Homie 5 device on the portable core.
//
//   <id>                 root device
//     switch/on          boolean, settable: what a controller switches
//     sensor/temperature float, read-only: a simulated reading, every --period-ms
//   <id>-child           child device
//     status/uptime      integer seconds, published from a worker thread
//
// The tree is built before the first connect. Each connect flushes what was held while
// the link was down and re-subscribes the /set topics (PahoTransport); the first then
// publishes the whole tree, a later one re-asserts $state over the broker's "lost".
#include <ebus_posix/paho_transport.h>
#include <ebus_posix/posix_port.h>
#include <homie/Device.h>
#include <homie/homie.h>
#include <homie/homie_clock.h>
#include <homie/homie_id.h>
#include <homie/homie_log.h>
#include <homie/Node.h>
#include <homie/Property.h>
#include <atomic>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include "cli.h"

static PahoTransport transport;   // static: it holds its buffers

static Device root;
static Device child;
static Property switch_on;
static Property temperature;
static Property uptime;

static std::atomic<bool> running{true};
static std::thread uptime_thread;

static void on_signal(int) { running.store(false); }

// The settable's application side: the core has already validated and stored the value.
// Returning false refuses it (the core puts the old value back and publishes nothing).
static bool on_switch_set(void* ctx, const char* value) {
    (void)ctx;
    printf("switch/on set to %s\n", value);
    fflush(stdout);
    return true;
}

// Any-thread publishing: this thread never touches the client; publish_queued() hands the
// value to PahoTransport::queue_publish(), which loop() sends.
static void uptime_worker() {
    uint32_t start = homie_now_ms();
    while (running.load()) {
        homie_sleep_ms(1000);
        uptime.setValue((int64_t)((homie_now_ms() - start) / 1000));
        uptime.publish_queued();
    }
}

static void on_connected(void* ctx, bool first) {
    (void)ctx;
    if (first) {
        root.publishTree();   // child init -> $description -> ready, then the root
        uptime_thread = std::thread(uptime_worker);
    } else {
        root.publishStateTree();
    }
}

static void usage(const char* argv0) {
    fprintf(stderr,
            "usage: %s [options]\n"
            "%s"
            "  --device-id ID       root device id (default posix-demo)\n"
            "  --period-ms MS       temperature update period (default 2000)\n",
            argv0, BROKER_USAGE);
}

int main(int argc, char** argv) {
    BrokerArgs broker;
    const char* device_id = "posix-demo";
    unsigned period_ms = 2000;
    for (int i = 1; i < argc; i++) {
        const char* v;
        if (parse_broker_flag(argc, argv, &i, &broker)) continue;
        if ((v = flag_value(argc, argv, &i, "--device-id"))) { device_id = v; continue; }
        if ((v = flag_value(argc, argv, &i, "--period-ms"))) {
            period_ms = (unsigned)atoi(v);
            continue;
        }
        usage(argv[0]);
        return strcmp(argv[i], "--help") == 0 ? 0 : 2;
    }
    broker_args_finish(&broker);

    char legal[HOMIE_DEVICE_ID_MAX + 1];
    sanitize_homie_id(device_id, legal, sizeof(legal));
    if (strcmp(legal, device_id) != 0) {
        fprintf(stderr, "--device-id '%s' is not a Homie id (a-z, 0-9, '-'; at most %d)\n",
                device_id, HOMIE_DEVICE_ID_MAX);
        return 2;
    }
    if (!homie_set_topic_domain(broker.domain)) {
        fprintf(stderr, "--domain '%s' is not a Homie id of at most %d chars\n",
                broker.domain, HOMIE_TOPIC_PREFIX_MAX - 2);
        return 2;
    }

    ebus_posix_port_init(&transport, broker.quiet);

    // The tree, root first so the child can take its transport.
    root.init("POSIX demo", device_id, "example.posix-demo", &transport);
    Node* sw = root.addNode("switch", "Switch", "energy.ebus.capability.switch");
    sw->addProperty(&switch_on, "on", "On", PropertyDatatype::Boolean, "", true);
    switch_on.setValue(false);
    Node* sensor = root.addNode("sensor", "Sensor", "example.temperature");
    sensor->addProperty(&temperature, "temperature", "Temperature", PropertyDatatype::Float,
                        to_homie(Unit::DegreeCelsius));
    temperature.setValue(20.0f);

    char child_id[HOMIE_DEVICE_ID_MAX + 1];
    make_homie_child_id(device_id, "child", child_id, sizeof(child_id));
    child.init("POSIX demo child", child_id, "example.posix-demo-child", root.mqttClient());
    Node* status = child.addNode("status", "Status", "energy.ebus.capability.status");
    status->addProperty(&uptime, "uptime", "Uptime", PropertyDatatype::Integer,
                        to_homie(Unit::Second));
    uptime.setValue((int64_t)0);
    root.addChild(&child);

    ebus_posix_register_settable(&switch_on, on_switch_set, nullptr);

    char will_topic[HOMIE_TOPIC_MAX + 1];
    snprintf(will_topic, sizeof(will_topic), "%s%s", root.topic(), HOMIE_$STATE);
    PahoConfig config;
    config.host = broker.host;
    config.port = broker.port;
    config.client_id = device_id;
    config.username = broker.user;
    config.password = broker.password;
    config.will_topic = will_topic;
    config.will_payload = HOMIE_STATE_LOST;
    config.reconnect_interval_ms = broker.reconnect_ms;
    if (!transport.begin(config)) return 1;
    transport.on_connected(on_connected, nullptr);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    uint32_t next_reading = homie_now_ms() + period_ms;
    while (running.load()) {
        transport.loop();
        uint32_t now = homie_now_ms();
        if ((int32_t)(now - next_reading) >= 0) {
            next_reading = now + period_ms;
            // A slow sine around 21 degrees, so every reading differs from the last.
            temperature.setValue((float)(21.0 + 3.0 * sin(now / 10000.0)));
            temperature.publish_value();
        }
        homie_sleep_ms(10);
    }

    if (uptime_thread.joinable()) uptime_thread.join();
    if (transport.connected()) root.setState(DEVICE_STATE_DISCONNECTED);
    transport.disconnect(1000);
    return 0;
}
