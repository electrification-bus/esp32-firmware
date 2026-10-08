// SPDX-License-Identifier: MIT
// ebus-posix-controller: discovers Homie 5 devices in one domain and prints them and every
// change to their state and property values, one line each on stdout:
//
//   state <device> <state>
//   device <device> name="<name>" type=<type> parent=<parent-or-dash> nodes=<n>
//   property <device>/<node>/<property> = <value>
//   set <device>/<node>/<property> = <value>
//
// --set DEVICE/NODE/PROPERTY=VALUE sends one /set once that property is discovered.
#include <ebus_posix/paho_transport.h>
#include <ebus_posix/posix_port.h>
#include <homie/controller.h>
#include <homie/Device.h>
#include <homie/homie_clock.h>
#include <atomic>
#include <map>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include "cli.h"

static PahoTransport transport;
static std::atomic<bool> running{true};

static void on_signal(int) { running.store(false); }

static void on_connected(void* ctx, bool first) {
    (void)ctx;
    (void)first;
    controller_setup_discovery();   // every connect: the broker resends the retained topics
}

struct PendingSet {
    std::string device, node, property, value;
    bool sent = false;
};

static bool parse_set(const char* arg, PendingSet* out) {
    std::string s(arg);
    size_t eq = s.find('=');
    if (eq == std::string::npos) return false;
    std::string path = s.substr(0, eq);
    size_t a = path.find('/');
    size_t b = a == std::string::npos ? a : path.find('/', a + 1);
    if (b == std::string::npos || path.find('/', b + 1) != std::string::npos) return false;
    out->device = path.substr(0, a);
    out->node = path.substr(a + 1, b - a - 1);
    out->property = path.substr(b + 1);
    out->value = s.substr(eq + 1);
    return !out->device.empty() && !out->node.empty() && !out->property.empty();
}

// What has been printed, to print only changes.
static std::map<std::string, DeviceState> seen_state;
static std::map<std::string, uint32_t> seen_description;
static std::map<std::string, std::string> seen_value;

static void report() {
    ControllerDevice infos[MAX_DISCOVERED_DEVICES];
    int n = controller_list_device_info(infos, MAX_DISCOVERED_DEVICES);
    for (int i = 0; i < n; i++) {
        ControllerDevice& info = infos[i];
        Device* dev = info.device;
        if (!dev) continue;
        std::string id = dev->getId();

        auto st = seen_state.find(id);
        if (st == seen_state.end() || st->second != info.state) {
            seen_state[id] = info.state;
            printf("state %s %s\n", id.c_str(), device_state_to_cstr(info.state));
        }
        if (info.has_description) {
            auto d = seen_description.find(id);
            if (d == seen_description.end() || d->second != info.description_hash) {
                seen_description[id] = info.description_hash;
                printf("device %s name=\"%s\" type=%s parent=%s nodes=%d\n", id.c_str(),
                       dev->name(), dev->type(), info.parent_id[0] ? info.parent_id : "-",
                       dev->numNodes());
            }
        }
        for (int ni = 0; ni < dev->numNodes(); ni++) {
            Node* node = dev->nodeAt(ni);
            for (int pi = 0; pi < node->numProperties(); pi++) {
                Property* p = node->propertyAt(pi);
                if (!p->value()[0]) continue;   // no value yet
                std::string key = id + "/" + node->id() + "/" + p->id();
                auto v = seen_value.find(key);
                if (v == seen_value.end() || v->second != p->value()) {
                    seen_value[key] = p->value();
                    printf("property %s = %s\n", key.c_str(), p->value());
                }
            }
        }
    }
    fflush(stdout);
}

// Device::getNode() and Node::getProperty() log every miss, so a property that is not
// discovered yet is looked for by iterating instead.
static Property* find_property(const PendingSet& s) {
    Device* dev = controller_get_device(s.device.c_str());
    if (!dev) return nullptr;
    for (int ni = 0; ni < dev->numNodes(); ni++) {
        Node* node = dev->nodeAt(ni);
        if (s.node != node->id()) continue;
        for (int pi = 0; pi < node->numProperties(); pi++) {
            if (s.property == node->propertyAt(pi)->id()) return node->propertyAt(pi);
        }
    }
    return nullptr;
}

static void usage(const char* argv0) {
    fprintf(stderr,
            "usage: %s [options]\n"
            "%s"
            "  --set D/N/P=VALUE    send VALUE to device D, node N, property P once it is "
            "discovered\n"
            "  --duration-s S       exit after S seconds (default: run until interrupted)\n",
            argv0, BROKER_USAGE);
}

int main(int argc, char** argv) {
    BrokerArgs broker;
    PendingSet pending;
    bool have_set = false;
    unsigned duration_s = 0;
    for (int i = 1; i < argc; i++) {
        const char* v;
        if (parse_broker_flag(argc, argv, &i, &broker)) continue;
        if ((v = flag_value(argc, argv, &i, "--set"))) {
            if (!parse_set(v, &pending)) {
                fprintf(stderr, "--set wants DEVICE/NODE/PROPERTY=VALUE, got '%s'\n", v);
                return 2;
            }
            have_set = true;
            continue;
        }
        if ((v = flag_value(argc, argv, &i, "--duration-s"))) {
            duration_s = (unsigned)atoi(v);
            continue;
        }
        usage(argv[0]);
        return strcmp(argv[i], "--help") == 0 ? 0 : 2;
    }
    broker_args_finish(&broker);
    if (strlen(broker.domain) > CONTROLLER_DOMAIN_MAX - 2) {
        fprintf(stderr, "--domain '%s' is longer than %d chars\n", broker.domain,
                CONTROLLER_DOMAIN_MAX - 2);
        return 2;
    }

    ebus_posix_port_init(&transport, broker.quiet);
    controller_init(&transport, broker.domain, false);

    char client_id[HOMIE_DEVICE_ID_MAX + 1];
    snprintf(client_id, sizeof(client_id), "ebus-posix-controller-%ld", (long)getpid());
    PahoConfig config;
    config.host = broker.host;
    config.port = broker.port;
    config.client_id = client_id;
    config.username = broker.user;
    config.password = broker.password;
    config.reconnect_interval_ms = broker.reconnect_ms;
    if (!transport.begin(config)) return 1;
    transport.on_connected(on_connected, nullptr);
    transport.set_fallback(controller_mqtt_callback);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    uint32_t start = homie_now_ms();
    uint32_t next_report = start;
    while (running.load()) {
        transport.loop();
        controller_loop();
        uint32_t now = homie_now_ms();
        if ((int32_t)(now - next_report) < 0) {
            homie_sleep_ms(10);
            continue;
        }
        next_report = now + 100;
        report();
        // After report(), so the property's current value is printed before the /set.
        if (have_set && !pending.sent && transport.connected()) {
            Property* p = find_property(pending);
            if (p && p->settable() &&
                controller_set_property(pending.device.c_str(), pending.node.c_str(),
                                        pending.property.c_str(), pending.value.c_str())) {
                pending.sent = true;
                printf("set %s/%s/%s = %s\n", pending.device.c_str(), pending.node.c_str(),
                       pending.property.c_str(), pending.value.c_str());
                fflush(stdout);
            }
        }
        if (duration_s && now - start >= duration_s * 1000u) break;
    }
    report();
    transport.disconnect(1000);
    return 0;
}
