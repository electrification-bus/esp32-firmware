// Host-side tests for the TCP log server in src/platform/log_stream.cpp, over real
// loopback sockets. log_stream.cpp is compiled into this suite directly, against the
// Arduino.h, FreeRTOS and lwIP stand-ins next to this file.

#define LOG_CAPTURE_ESP_LOG 0   // no ESP_LOGx hook on the host
#include <unity.h>
#include "../../../src/platform/log_stream.cpp"

#include <signal.h>
#include <time.h>

HostConsole host_console;

static int s_client = -1;

static void pause_ms(int ms) {
    struct timespec ts = {0, (long)ms * 1000000L};
    nanosleep(&ts, NULL);
}

// A free loopback port: bind port 0, read back what the kernel chose, release it.
static uint16_t free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr*)&a, sizeof(a));
    socklen_t len = sizeof(a);
    getsockname(fd, (struct sockaddr*)&a, &len);
    close(fd);
    return ntohs(a.sin_port);
}

static int connect_client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = htons(port);
    TEST_ASSERT_EQUAL_INT(0, connect(fd, (struct sockaddr*)&a, sizeof(a)));
    set_nonblocking(fd);
    return fd;
}

// Run the server loop for a while, appending whatever the client receives to out.
static size_t pump(int fd, char* out, size_t cap, size_t have) {
    for (int i = 0; i < 50; i++) {
        log_tcp_loop();
        pause_ms(2);
        for (;;) {
            if (have + 1 >= cap) break;
            ssize_t n = recv(fd, out + have, cap - 1 - have, 0);
            if (n <= 0) break;
            have += (size_t)n;
        }
    }
    out[have] = '\0';
    return have;
}

static void fill_ring(int lines) {
    for (int i = 0; i < lines; i++) logStream.printf("boot line %04d ............................\n", i);
}

void setUp(void) {
    log_tcp_begin(free_port());
    TEST_ASSERT_TRUE(log_tcp_running());
}

void tearDown(void) {
    if (s_client >= 0) close(s_client);
    s_client = -1;
    log_tcp_stop();
}

// `nc host 23 </dev/null` shuts down its sending side right after connecting and keeps
// reading. That FIN must not end the stream after the banner.
static void test_half_closed_monitor_gets_replay_and_live_lines(void) {
    fill_ring(300);
    s_client = connect_client(log_tcp_get_port());
    TEST_ASSERT_EQUAL_INT(0, shutdown(s_client, SHUT_WR));

    static char got[32768];
    size_t n = pump(s_client, got, sizeof(got), 0);
    TEST_ASSERT_NOT_NULL(strstr(got, "<< esp32 log stream: replaying"));
    TEST_ASSERT_NOT_NULL(strstr(got, "boot line 0299"));
    TEST_ASSERT_EQUAL_INT(1, log_tcp_clients());

    logStream.printf("live line after half-close\n");
    pump(s_client, got, sizeof(got), n);
    TEST_ASSERT_NOT_NULL(strstr(got, "live line after half-close"));
}

// The "monitor connected" line used to be logged after the replay start was taken, so
// with a full ring it pushed the oldest replay bytes out and every monitor was greeted
// with a "bytes dropped" note.
static void test_connect_on_full_ring_reports_no_drop(void) {
    fill_ring(300);
    s_client = connect_client(log_tcp_get_port());

    static char got[32768];
    pump(s_client, got, sizeof(got), 0);
    TEST_ASSERT_NOT_NULL(strstr(got, "LOG: monitor connected"));
    TEST_ASSERT_NULL(strstr(got, "bytes dropped"));
}

// A monitor that is fully gone is reaped by the send error its RST produces.
static void test_closed_monitor_is_reaped(void) {
    s_client = connect_client(log_tcp_get_port());
    static char got[32768];
    pump(s_client, got, sizeof(got), 0);
    TEST_ASSERT_EQUAL_INT(1, log_tcp_clients());

    close(s_client);
    s_client = -1;
    for (int i = 0; i < 50 && log_tcp_clients() > 0; i++) {
        logStream.printf("line %d while the monitor is gone\n", i);
        log_tcp_loop();
        pause_ms(2);
    }
    TEST_ASSERT_EQUAL_INT(0, log_tcp_clients());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    signal(SIGPIPE, SIG_IGN);   // a send() to a reset peer must fail, not kill the suite
    logStream.begin(115200);
    UNITY_BEGIN();
    RUN_TEST(test_half_closed_monitor_gets_replay_and_live_lines);
    RUN_TEST(test_connect_on_full_ring_reports_no_drop);
    RUN_TEST(test_closed_monitor_is_reaped);
    return UNITY_END();
}
