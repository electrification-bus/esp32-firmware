// SPDX-License-Identifier: MIT
// End-to-end tests: a real mosquitto on a free localhost port, the demo programs as child
// processes, and assertions on what reaches the broker, read by a Paho client.
//
//   ebus_posix_e2e <case> --mosquitto PATH --device PATH --controller PATH --workdir DIR
//
// Cases: boot, set, invalid, will, reconnect, controller.
#include <MQTTClient.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

static const char* DEVICE_ID = "e2e-dev";
static const char* CHILD_ID = "e2e-dev-child";
static const std::string ROOT_T = std::string("ebus/5/") + DEVICE_ID + "/";
static const std::string CHILD_T = std::string("ebus/5/") + CHILD_ID + "/";

static std::string g_mosquitto, g_device, g_controller, g_dir;
static std::vector<pid_t> g_children;

#define REQUIRE(cond, ...)                                                       \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: FAILED: %s\n  ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                        \
            fprintf(stderr, "\n  logs in %s\n", g_dir.c_str());                  \
            exit(1);                                                             \
        }                                                                        \
    } while (0)

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void kill_children() {
    for (pid_t p : g_children) {
        if (p > 0) kill(p, SIGKILL);
    }
}

// ---- processes --------------------------------------------------------------------------

static pid_t spawn(const std::vector<std::string>& args, const std::string& log) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back((char*)a.c_str());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = -1;
    int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    REQUIRE(rc == 0, "spawn %s: %s", argv[0], strerror(rc));
    g_children.push_back(pid);
    return pid;
}

// Wait for `pid` to exit; true if it did within timeout_ms.
static bool wait_exit(pid_t pid, int timeout_ms, int* status = nullptr) {
    for (int waited = 0; waited <= timeout_ms; waited += 20) {
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            if (status) *status = st;
            for (pid_t& p : g_children) {
                if (p == pid) p = 0;
            }
            return true;
        }
        sleep_ms(20);
    }
    return false;
}

static void stop(pid_t pid, int sig) {
    kill(pid, sig);
    REQUIRE(wait_exit(pid, 10000), "pid %d did not exit on signal %d", (int)pid, sig);
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int free_port() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    REQUIRE(bind(s, (sockaddr*)&a, sizeof(a)) == 0, "bind: %s", strerror(errno));
    socklen_t len = sizeof(a);
    getsockname(s, (sockaddr*)&a, &len);
    close(s);
    return ntohs(a.sin_port);
}

static bool port_open(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    bool ok = connect(s, (sockaddr*)&a, sizeof(a)) == 0;
    close(s);
    return ok;
}

struct Broker {
    int port = 0;
    pid_t pid = -1;
    void start() {
        if (!port) port = free_port();
        std::string conf = g_dir + "/mosquitto.conf";
        std::ofstream(conf) << "listener " << port << " 127.0.0.1\n"
                            << "allow_anonymous true\n"
                            << "persistence false\n";
        pid = spawn({g_mosquitto, "-c", conf}, g_dir + "/mosquitto.log");
        for (int i = 0; i < 250 && !port_open(port); i++) sleep_ms(20);
        REQUIRE(port_open(port), "mosquitto did not listen on %d", port);
    }
    void stop_broker() { stop(pid, SIGTERM); }
};

static pid_t start_device(int port) {
    return spawn({g_device, "--host", "127.0.0.1", "--port", std::to_string(port), "--device-id", DEVICE_ID,
                  "--period-ms", "500", "--reconnect-ms", "300"},
                 g_dir + "/device.log");
}

// ---- a recording MQTT client ---------------------------------------------------------

struct Msg {
    std::string topic, payload;
    bool retained;
};

class Recorder {
 public:
    explicit Recorder(int port) {
        static int n = 0;
        std::string uri = "tcp://127.0.0.1:" + std::to_string(port);
        std::string id = "e2e-rec-" + std::to_string(getpid()) + "-" + std::to_string(n++);
        MQTTClient_create(&_c, uri.c_str(), id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
        MQTTClient_setCallbacks(_c, this, nullptr, arrived, nullptr);
        MQTTClient_connectOptions o = MQTTClient_connectOptions_initializer;
        o.cleansession = 1;
        int rc = MQTTClient_connect(_c, &o);
        REQUIRE(rc == MQTTCLIENT_SUCCESS, "recorder connect: %d", rc);
        rc = MQTTClient_subscribe(_c, "ebus/5/#", 2);
        REQUIRE(rc >= 0 && rc <= 2, "recorder subscribe: %d", rc);
    }
    ~Recorder() {
        MQTTClient_disconnect(_c, 200);
        MQTTClient_destroy(&_c);
    }

    void publish(const std::string& topic, const std::string& payload) {
        MQTTClient_message m = MQTTClient_message_initializer;
        m.payload = (void*)payload.data();
        m.payloadlen = (int)payload.size();
        m.qos = 0;
        m.retained = 0;
        int rc = MQTTClient_publishMessage(_c, topic.c_str(), &m, nullptr);
        REQUIRE(rc == MQTTCLIENT_SUCCESS, "publish %s: %d", topic.c_str(), rc);
    }

    std::vector<Msg> snapshot() {
        std::lock_guard<std::mutex> l(_lock);
        return _msgs;
    }
    size_t size() {
        std::lock_guard<std::mutex> l(_lock);
        return _msgs.size();
    }

    // Index of the first message at or after `from` matching topic (and payload, unless
    // null), waiting up to timeout_ms; -1 if none came.
    int wait_for(const std::string& topic, const char* payload, size_t from = 0,
                 int timeout_ms = 10000) {
        for (int waited = 0; waited <= timeout_ms; waited += 20) {
            int i = find(topic, payload, from);
            if (i >= 0) return i;
            sleep_ms(20);
        }
        return -1;
    }
    int find(const std::string& topic, const char* payload, size_t from = 0) {
        std::lock_guard<std::mutex> l(_lock);
        for (size_t i = from; i < _msgs.size(); i++) {
            if (_msgs[i].topic == topic && (!payload || _msgs[i].payload == payload)) {
                return (int)i;
            }
        }
        return -1;
    }

 private:
    static int arrived(void* ctx, char* topic, int topic_len, MQTTClient_message* m) {
        Recorder* self = (Recorder*)ctx;
        std::string t = topic_len > 0 ? std::string(topic, (size_t)topic_len) : topic;
        {
            std::lock_guard<std::mutex> l(self->_lock);
            self->_msgs.push_back(
                {t, std::string((const char*)m->payload, (size_t)m->payloadlen), m->retained != 0});
        }
        MQTTClient_freeMessage(&m);
        MQTTClient_free(topic);
        return 1;
    }

    MQTTClient _c = nullptr;
    std::mutex _lock;
    std::vector<Msg> _msgs;
};

static void wait_ready(Recorder& rec) {
    REQUIRE(rec.wait_for(ROOT_T + "$state", "ready") >= 0,
            "the device never published root $state ready");
}

// ---- cases ------------------------------------------------------------------------------

// Boot: child init -> $description -> ready, then root init -> $description -> ready.
static void case_boot() {
    Broker b;
    b.start();
    Recorder rec(b.port);
    start_device(b.port);
    wait_ready(rec);

    std::vector<std::string> seq;
    std::string root_desc, child_desc;
    for (const Msg& m : rec.snapshot()) {
        for (const std::string* base : {&CHILD_T, &ROOT_T}) {
            const char* who = base == &CHILD_T ? "child" : "root";
            if (m.topic == *base + "$state") seq.push_back(std::string(who) + " " + m.payload);
            if (m.topic == *base + "$description") {
                seq.push_back(std::string(who) + " $description");
                (base == &CHILD_T ? child_desc : root_desc) = m.payload;
            }
        }
    }
    std::vector<std::string> want = {"child init", "child $description", "child ready",
                                     "root init",  "root $description",  "root ready"};
    std::string got;
    for (const std::string& s : seq) got += "[" + s + "] ";
    REQUIRE(seq == want, "boot order: %s", got.c_str());
    REQUIRE(root_desc.find("\"children\":[\"e2e-dev-child\"]") != std::string::npos,
            "root $description: %s", root_desc.c_str());
    REQUIRE(child_desc.find("\"root\":\"e2e-dev\"") != std::string::npos &&
                child_desc.find("\"parent\":\"e2e-dev\"") != std::string::npos,
            "child $description: %s", child_desc.c_str());
    REQUIRE(rec.find(ROOT_T + "switch/on", "false") >= 0, "no initial switch/on value");
    printf("boot: child init/$description/ready, then root init/$description/ready\n");
}

// /set of the boolean changes the value, both ways.
static void case_set() {
    Broker b;
    b.start();
    Recorder rec(b.port);
    start_device(b.port);
    wait_ready(rec);

    for (const char* v : {"true", "false"}) {
        size_t mark = rec.size();
        rec.publish(ROOT_T + "switch/on/set", v);
        REQUIRE(rec.wait_for(ROOT_T + "switch/on", v, mark) >= 0,
                "switch/on did not become %s", v);
    }
    std::string log = read_file(g_dir + "/device.log");
    REQUIRE(log.find("switch/on set to true") != std::string::npos,
            "the application handler did not run");
    printf("set: switch/on followed /set true, then false\n");
}

// An invalid payload is refused: nothing is published for it, and the next valid /set
// still works.
static void case_invalid() {
    Broker b;
    b.start();
    Recorder rec(b.port);
    start_device(b.port);
    wait_ready(rec);

    size_t mark = rec.size();
    rec.publish(ROOT_T + "switch/on/set", "maybe");
    rec.publish(ROOT_T + "switch/on/set", "true");
    int i = rec.wait_for(ROOT_T + "switch/on", "true", mark);
    REQUIRE(i >= 0, "the valid /set after the invalid one was not applied");
    std::vector<Msg> msgs = rec.snapshot();
    int values = 0;
    for (size_t k = mark; k < msgs.size(); k++) values += msgs[k].topic == ROOT_T + "switch/on";
    REQUIRE(values == 1, "%d switch/on publishes after the invalid /set, want 1", values);
    std::string log = read_file(g_dir + "/device.log");
    REQUIRE(log.find("invalid boolean value 'maybe'") != std::string::npos,
            "the refusal was not logged");
    REQUIRE(log.find("switch/on set to maybe") == std::string::npos,
            "the handler saw the invalid value");
    printf("invalid: 'maybe' refused and not published; the next /set applied\n");
}

// Last Will: a killed device's root $state becomes "lost".
static void case_will() {
    Broker b;
    b.start();
    Recorder rec(b.port);
    pid_t dev = start_device(b.port);
    wait_ready(rec);

    size_t mark = rec.size();
    stop(dev, SIGKILL);
    REQUIRE(rec.wait_for(ROOT_T + "$state", "lost", mark) >= 0,
            "no root $state lost after SIGKILL");
    printf("will: root $state lost after SIGKILL\n");
}

// Broker restart (retained store lost): the device reconnects, flushes what it held,
// re-subscribes /set, then re-asserts $state.
static void case_reconnect() {
    Broker b;
    b.start();
    {
        Recorder rec(b.port);
        start_device(b.port);
        wait_ready(rec);
    }
    b.stop_broker();
    sleep_ms(2000);   // several temperature and uptime updates happen while down
    b.start();        // same port, empty retained store
    Recorder rec(b.port);

    REQUIRE(rec.wait_for(ROOT_T + "$state", "ready") >= 0,
            "root $state ready was not republished after the restart");
    REQUIRE(rec.wait_for(CHILD_T + "$state", "ready") >= 0,
            "child $state ready was not republished after the restart");

    bool applied = false;
    for (int attempt = 0; attempt < 20 && !applied; attempt++) {
        size_t mark = rec.size();
        rec.publish(ROOT_T + "switch/on/set", "true");
        applied = rec.wait_for(ROOT_T + "switch/on", "true", mark, 500) >= 0;
    }
    REQUIRE(applied, "/set not handled after the restart: not re-subscribed");

    std::string log = read_file(g_dir + "/device.log");
    size_t lost = log.find("PAHO: connection lost");
    REQUIRE(lost != std::string::npos, "the device did not see the link drop");
    size_t flushed = log.find("PAHO: flushed", lost);
    size_t resub = log.find("PAHO: resubscribed 1 of 1 settable topics", lost);
    size_t notify = log.find("PAHO: connected (reconnect)", lost);
    REQUIRE(flushed != std::string::npos && resub != std::string::npos &&
                notify != std::string::npos && flushed < resub && resub < notify,
            "reconnect order is not flush, resubscribe, notify");
    // Two retained topics changed while down (temperature, uptime), several times each;
    // newest-per-topic holds at most one entry for each.
    int sent = -1, held = -1;
    sscanf(log.c_str() + flushed, "PAHO: flushed %d of %d", &sent, &held);
    REQUIRE(held >= 1 && held <= 2 && sent == held, "flushed %d of %d held", sent, held);
    printf("reconnect: flushed %d held (newest per topic), resubscribed /set, re-asserted "
           "$state\n", held);
}

// The controller discovers the device tree and sees a property change it caused.
static void case_controller() {
    Broker b;
    b.start();
    Recorder rec(b.port);
    start_device(b.port);
    wait_ready(rec);

    std::string out = g_dir + "/controller.out";
    pid_t ctl = spawn({g_controller, "--host", "127.0.0.1", "--port", std::to_string(b.port),
                       "--set",
                       std::string(DEVICE_ID) + "/switch/on=true", "--duration-s", "4"},
                      out);
    int status = 0;
    REQUIRE(wait_exit(ctl, 20000, &status), "the controller did not exit");
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0, "controller exit status %d", status);

    std::string text = read_file(out);
    REQUIRE(text.find("state e2e-dev ready") != std::string::npos, "root state: %s", text.c_str());
    REQUIRE(text.find("device e2e-dev name=\"POSIX demo\"") != std::string::npos,
            "root not described");
    REQUIRE(text.find("device e2e-dev-child name=\"POSIX demo child\"") != std::string::npos &&
                text.find("parent=e2e-dev ") != std::string::npos,
            "child not described with its parent");
    size_t set = text.find("set e2e-dev/switch/on = true");
    REQUIRE(set != std::string::npos, "the controller did not send its /set");
    size_t before = text.find("property e2e-dev/switch/on = false");
    REQUIRE(before != std::string::npos && before < set,
            "the controller did not report switch/on false before its /set");
    REQUIRE(text.find("property e2e-dev/switch/on = true", set) != std::string::npos,
            "the controller did not see switch/on change after its /set");
    int temps = 0;
    for (size_t p = text.find("property e2e-dev/sensor/temperature = "); p != std::string::npos;
         p = text.find("property e2e-dev/sensor/temperature = ", p + 1)) {
        temps++;
    }
    REQUIRE(temps >= 2, "%d temperature changes seen, want at least 2", temps);
    printf("controller: discovered root and child, sent /set, saw switch/on and %d "
           "temperature changes\n", temps);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <case> --mosquitto P --device P --controller P --workdir D\n",
                argv[0]);
        return 2;
    }
    std::string which = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--mosquitto") g_mosquitto = v;
        else if (k == "--device") g_device = v;
        else if (k == "--controller") g_controller = v;
        else if (k == "--workdir") g_dir = v;
    }
    g_dir += "/" + which;
    mkdir(g_dir.substr(0, g_dir.rfind('/')).c_str(), 0755);
    mkdir(g_dir.c_str(), 0755);
    for (const char* f : {"/device.log", "/mosquitto.log", "/controller.out"}) {
        unlink((g_dir + f).c_str());
    }
    atexit(kill_children);
    signal(SIGPIPE, SIG_IGN);

    if (which == "boot") case_boot();
    else if (which == "set") case_set();
    else if (which == "invalid") case_invalid();
    else if (which == "will") case_will();
    else if (which == "reconnect") case_reconnect();
    else if (which == "controller") case_controller();
    else {
        fprintf(stderr, "unknown case '%s'\n", which.c_str());
        return 2;
    }
    return 0;
}
