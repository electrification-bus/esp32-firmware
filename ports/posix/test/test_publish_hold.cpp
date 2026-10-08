// SPDX-License-Identifier: MIT
// The disconnected-link rules (ebus_posix/publish_hold.h), without a broker.
#include <ebus_posix/publish_hold.h>
#include <stdio.h>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) {                                                \
            fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                               \
        }                                                             \
    } while (0)

struct Sent {
    std::string topic, payload;
    bool retained;
    int qos;
};

struct Sink {
    std::vector<Sent> sent;
    int fail_after = -1;   // refuse the send after this many
    static bool send(void* ctx, const char* topic, const char* payload, int length,
                     bool retained, int qos) {
        Sink* s = (Sink*)ctx;
        if (s->fail_after >= 0 && (int)s->sent.size() >= s->fail_after) return false;
        s->sent.push_back({topic, std::string(payload, (size_t)length), retained, qos});
        return true;
    }
};

static PublishHold hold;   // large: keep it off the stack

static void test_rules() {
    hold.clear();
    CHECK(hold.hold("a/v", "1", 1, true, 2) == PublishHold::HELD);
    CHECK(hold.hold("e/x", "ev1", 3, false, 1) == PublishHold::HELD);
    CHECK(hold.hold("e/q0", "gone", 4, false, 0) == PublishHold::DROPPED_QOS0);
    CHECK(hold.hold("b/v", "x", 1, true, 1) == PublishHold::HELD);
    CHECK(hold.hold("e/x", "ev2", 3, false, 2) == PublishHold::HELD);   // events never merge
    CHECK(hold.hold("a/v", "2", 1, true, 2) == PublishHold::REPLACED);  // newest per topic
    CHECK(hold.count() == 4);

    Sink sink;
    CHECK(hold.flush(Sink::send, &sink) == 4);
    CHECK(hold.count() == 0);
    // The replaced retained value moved to the back.
    const char* order[] = {"e/x:ev1", "b/v:x", "e/x:ev2", "a/v:2"};
    CHECK(sink.sent.size() == 4);
    for (size_t i = 0; i < sink.sent.size() && i < 4; i++) {
        CHECK(sink.sent[i].topic + ":" + sink.sent[i].payload == order[i]);
    }
    CHECK(sink.sent[3].retained && sink.sent[3].qos == 2);
}

static void test_empty_string_value_kept() {
    hold.clear();
    const char nul = 0;
    CHECK(hold.hold("a/s", &nul, 1, true, 2) == PublishHold::HELD);
    Sink sink;
    hold.flush(Sink::send, &sink);
    CHECK(sink.sent.size() == 1 && sink.sent[0].payload.size() == 1 &&
          sink.sent[0].payload[0] == 0);
}

static void test_bounded_evicts_oldest() {
    hold.clear();
    uint32_t evicted = hold.evicted();
    char topic[32];
    for (int i = 0; i < EBUS_POSIX_HOLD_ENTRIES + 3; i++) {
        snprintf(topic, sizeof(topic), "t/%d", i);
        hold.hold(topic, "v", 1, true, 2);
    }
    CHECK(hold.count() == EBUS_POSIX_HOLD_ENTRIES);
    CHECK(hold.evicted() - evicted == 3);
    Sink sink;
    hold.flush(Sink::send, &sink);
    CHECK(!sink.sent.empty() && sink.sent.front().topic == "t/3");
    snprintf(topic, sizeof(topic), "t/%d", EBUS_POSIX_HOLD_ENTRIES + 2);
    CHECK(!sink.sent.empty() && sink.sent.back().topic == topic);
}

static void test_too_large_dropped() {
    hold.clear();
    static char big[EBUS_POSIX_HOLD_PAYLOAD_MAX + 1];
    CHECK(hold.hold("d/$description", big, sizeof(big), true, 2) ==
          PublishHold::DROPPED_TOO_LARGE);
    CHECK(hold.count() == 0);
}

static void test_interrupted_flush_keeps_the_rest() {
    hold.clear();
    hold.hold("a", "1", 1, true, 2);
    hold.hold("b", "2", 1, true, 2);
    hold.hold("c", "3", 1, true, 2);
    Sink sink;
    sink.fail_after = 1;
    CHECK(hold.flush(Sink::send, &sink) == 1);
    CHECK(hold.count() == 2);
    // A newer value for a still-held topic replaces it.
    CHECK(hold.hold("c", "4", 1, true, 2) == PublishHold::REPLACED);
    Sink again;
    CHECK(hold.flush(Sink::send, &again) == 2);
    CHECK(again.sent.size() == 2 && again.sent[0].topic == "b" && again.sent[1].payload == "4");
}

int main() {
    test_rules();
    test_empty_string_value_kept();
    test_bounded_evicts_oldest();
    test_too_large_dropped();
    test_interrupted_flush_keeps_the_rest();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("publish_hold: all checks passed\n");
    return 0;
}
