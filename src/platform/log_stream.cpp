/*
 * log_stream.cpp — implementation of the UART + TCP console tee.
 *
 * Threading model
 *   write side  : any task (Serial.printf is called from the main loop, the modbus
 *                 worker on core 0, MQTT callbacks, AsyncTCP's task, ...). Guarded
 *                 by a portMUX spinlock held only for the memcpy into the ring.
 *   socket side : main loop only, via log_tcp_loop(). accept(), send() and recv()
 *                 are never called from anywhere else, so no socket locking needed.
 *
 * Bytes are staged out of the ring into a stack buffer *under* the lock and sent
 * outside it, so a writer on the other core cannot overwrite a buffer mid-send.
 *
 * Raw lwIP BSD sockets rather than WiFiServer/NetworkClient: no heap, no
 * shared_ptr, and it links the same whether the link is WiFi or Ethernet.
 */
#include <platform/log_stream.h>

// This translation unit talks to the hardware console object through
// log_console(); `Serial` must not resolve to our tee here (that would recurse).
#undef Serial

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

// ---------------------------------------------------------------------------
// Ring buffer
//
// s_head is a monotonic count of every byte ever accepted, so a client cursor is
// just an absolute position; "have I been overrun?" is a comparison, with no
// wrap-around arithmetic to get wrong. The 64-bit counter cannot realistically
// wrap (8 KB/s for ~70 million years).
// ---------------------------------------------------------------------------
#define RING_MASK (LOG_RING_SIZE - 1)
static_assert((LOG_RING_SIZE & RING_MASK) == 0, "LOG_RING_SIZE must be a power of two");

static uint8_t          s_ring[LOG_RING_SIZE];
static volatile uint64_t s_head = 0;
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool     s_uart_started = false;
static bool     s_uart_echo    = true;

// Oldest absolute position still present in the ring.
static inline uint64_t ring_oldest(uint64_t head) {
    return head > (uint64_t)LOG_RING_SIZE ? head - (uint64_t)LOG_RING_SIZE : 0;
}

// s_head is 64-bit on a 32-bit CPU, so an unlocked read can tear.
static uint64_t ring_head(void) {
    portENTER_CRITICAL(&s_mux);
    uint64_t h = s_head;
    portEXIT_CRITICAL(&s_mux);
    return h;
}

static void ring_write(const uint8_t* p, size_t n) {
    if (n == 0) return;
    // A single write larger than the ring can only leave its tail behind.
    if (n >= (size_t)LOG_RING_SIZE) {
        p += n - (size_t)LOG_RING_SIZE;
        n  = (size_t)LOG_RING_SIZE;
    }
    portENTER_CRITICAL(&s_mux);
    size_t off   = (size_t)(s_head & RING_MASK);
    size_t first = (size_t)LOG_RING_SIZE - off;
    if (first > n) first = n;
    memcpy(s_ring + off, p, first);
    if (n > first) memcpy(s_ring, p + first, n - first);
    s_head += n;
    portEXIT_CRITICAL(&s_mux);
}

// Copy up to want bytes starting at absolute position from into out.
// Returns bytes copied and advances *from past any overrun (reported via *skipped).
static size_t ring_read(uint64_t* from, uint8_t* out, size_t want, uint64_t* skipped) {
    *skipped = 0;
    portENTER_CRITICAL(&s_mux);
    uint64_t head   = s_head;
    uint64_t oldest = ring_oldest(head);
    if (*from < oldest) {            // slow client: the ring rolled over its cursor
        *skipped = oldest - *from;
        *from = oldest;
    }
    size_t avail = (size_t)(head - *from);
    size_t n     = avail < want ? avail : want;
    if (n) {
        size_t off   = (size_t)(*from & RING_MASK);
        size_t first = (size_t)LOG_RING_SIZE - off;
        if (first > n) first = n;
        memcpy(out, s_ring + off, first);
        if (n > first) memcpy(out + first, s_ring, n - first);
        *from += n;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

size_t log_snapshot(char* out, size_t out_len) {
    if (!out || out_len < 2) return 0;
    uint64_t head = ring_head();
    size_t   want = out_len - 1;
    uint64_t avail = head - ring_oldest(head);
    if ((uint64_t)want > avail) want = (size_t)avail;
    uint64_t from = head - want;
    uint64_t skipped = 0;
    size_t   n = ring_read(&from, (uint8_t*)out, want, &skipped);
    out[n] = '\0';
    return n;
}

// ---------------------------------------------------------------------------
// LogStream — the Serial replacement
// ---------------------------------------------------------------------------
LogStream logStream;

size_t LogStream::write(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    ring_write(buffer, size);
    // The UART object is only touched after begin(); before that its constructor
    // may not have run (global init order across translation units).
    if (s_uart_started && s_uart_echo) return log_console().write(buffer, size);
    return size;
}

size_t LogStream::write(uint8_t c) {
    return write(&c, 1);
}

int  LogStream::availableForWrite() { return s_uart_started ? log_console().availableForWrite() : LOG_RING_SIZE; }
void LogStream::flush()             { if (s_uart_started) log_console().flush(); }
int  LogStream::available()         { return s_uart_started ? log_console().available() : 0; }
int  LogStream::read()              { return s_uart_started ? log_console().read() : -1; }
int  LogStream::peek()              { return s_uart_started ? log_console().peek() : -1; }
void LogStream::setDebugOutput(bool en) { if (s_uart_started) log_console().setDebugOutput(en); }
void LogStream::setUartEcho(bool en)    { s_uart_echo = en; }
bool LogStream::uartEcho() const        { return s_uart_echo; }

void LogStream::end() {
    if (s_uart_started) log_console().end();
    s_uart_started = false;
}

void LogStream::begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) {
    (void)config; (void)rxPin; (void)txPin;   // see header: ignored by design
    begin(baud);
}

#if LOG_CAPTURE_ESP_LOG
static int (*s_prev_vprintf)(const char*, va_list) = nullptr;

/*
 * ESP_LOGx / arduino-esp32's log_e()/log_i() do not go through Print, so they
 * would otherwise be missing from the TCP stream. Mirror them into the ring and
 * hand off to the previous handler so the UART copy is unchanged.
 */
static int log_esp_vprintf(const char* fmt, va_list ap) {
    if (!xPortInIsrContext()) {
        char    line[LOG_ESP_LINE_MAX];
        va_list ap2;
        va_copy(ap2, ap);
        int n = vsnprintf(line, sizeof(line), fmt, ap2);
        va_end(ap2);
        if (n > 0) {
            size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
            ring_write((const uint8_t*)line, len);
        }
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}
#endif

void LogStream::begin(unsigned long baud) {
    log_console().begin(baud);
    s_uart_started = true;
#if LOG_CAPTURE_ESP_LOG
    if (!s_prev_vprintf) s_prev_vprintf = esp_log_set_vprintf(log_esp_vprintf);
#endif
}

// ---------------------------------------------------------------------------
// TCP server
// ---------------------------------------------------------------------------
typedef struct {
    int      fd;        // -1 = free slot
    uint64_t tail;      // absolute ring position this client has been sent up to
    uint32_t dropped;   // cumulative bytes lost to overrun
} LogClient;

static LogClient s_clients[LOG_MAX_CLIENTS];
static int       s_listen_fd = -1;
static uint16_t  s_port      = 0;
static bool      s_slots_init = false;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Best-effort out-of-band note to one client (banner, overrun marker). Never
// blocks and never enters the ring, so it cannot feed back into the log.
static void client_note(int fd, const char* msg) {
    (void)send(fd, msg, strlen(msg), MSG_DONTWAIT);
}

static void client_close(LogClient* c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->tail = 0;
    c->dropped = 0;
}

static void clients_init_once(void) {
    if (s_slots_init) return;
    for (int i = 0; i < LOG_MAX_CLIENTS; i++) s_clients[i].fd = -1;
    s_slots_init = true;
}

void log_tcp_begin(uint16_t port) {
    clients_init_once();
    log_tcp_stop();
    if (port == 0) {
        logStream.printf("LOG: TCP log server disabled (port 0)\n");
        return;
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        logStream.printf("LOG: socket() failed, errno=%d\n", errno);
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    set_nonblocking(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        logStream.printf("LOG: bind(%u) failed, errno=%d\n", port, errno);
        close(fd);
        return;
    }
    if (listen(fd, LOG_MAX_CLIENTS) < 0) {
        logStream.printf("LOG: listen() failed, errno=%d\n", errno);
        close(fd);
        return;
    }
    s_listen_fd = fd;
    s_port      = port;
    logStream.printf("LOG: serial-over-TCP on port %u  (pio device monitor --port socket://<ip>:%u)\n",
                     port, port);
}

void log_tcp_stop(void) {
    clients_init_once();
    for (int i = 0; i < LOG_MAX_CLIENTS; i++) client_close(&s_clients[i]);
    if (s_listen_fd >= 0) close(s_listen_fd);
    s_listen_fd = -1;
    s_port = 0;
}

bool     log_tcp_running(void)  { return s_listen_fd >= 0; }
uint16_t log_tcp_get_port(void) { return s_port; }

int log_tcp_clients(void) {
    clients_init_once();
    int n = 0;
    for (int i = 0; i < LOG_MAX_CLIENTS; i++) if (s_clients[i].fd >= 0) n++;
    return n;
}

static void accept_pending(void) {
    for (;;) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int fd = accept(s_listen_fd, (struct sockaddr*)&peer, &peer_len);
        if (fd < 0) return;                       // EWOULDBLOCK: nothing pending

        set_nonblocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        // Keepalive so a monitor that vanished (laptop closed, VPN dropped) frees
        // its slot instead of holding it until the next reboot.
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        int idle = 30, intvl = 10, cnt = 3;
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));

        LogClient* slot = NULL;
        for (int i = 0; i < LOG_MAX_CLIENTS; i++) {
            if (s_clients[i].fd < 0) { slot = &s_clients[i]; break; }
        }
        if (!slot) {
            // All slots busy: evict the one furthest behind rather than refusing
            // the new connection, so a wedged client can't lock everyone out.
            slot = &s_clients[0];
            for (int i = 1; i < LOG_MAX_CLIENTS; i++) {
                if (s_clients[i].tail < slot->tail) slot = &s_clients[i];
            }
            client_note(slot->fd, "\r\n<< log: evicted, another monitor connected >>\r\n");
            client_close(slot);
        }

        slot->fd      = fd;
        slot->dropped = 0;
        // Start at the oldest retained byte: a monitor started after boot still
        // gets the boot log.
        uint64_t head = ring_head();
        slot->tail = ring_oldest(head);

        char banner[96];
        snprintf(banner, sizeof(banner),
                 "\r\n<< esp32 log stream: replaying %u buffered bytes >>\r\n",
                 (unsigned)(head - slot->tail));
        client_note(fd, banner);

        logStream.printf("LOG: monitor connected from %u.%u.%u.%u\n",
                         (unsigned)(peer.sin_addr.s_addr & 0xff),
                         (unsigned)((peer.sin_addr.s_addr >> 8) & 0xff),
                         (unsigned)((peer.sin_addr.s_addr >> 16) & 0xff),
                         (unsigned)((peer.sin_addr.s_addr >> 24) & 0xff));
    }
}

// Discard anything the monitor types (and detect a clean close). Keystrokes are
// not forwarded to the UART: this is a log tap, not a remote console.
// Returns false if the peer closed the connection.
static bool drain_rx(LogClient* c) {
    uint8_t junk[64];
    for (int i = 0; i < 8; i++) {          // bounded: a chatty peer must not hog the loop
        int n = recv(c->fd, junk, sizeof(junk), MSG_DONTWAIT);
        if (n > 0) continue;
        if (n == 0) return false;                                  // orderly close
        return (errno == EWOULDBLOCK || errno == EAGAIN);          // else: broken
    }
    return true;
}

static void flush_client(LogClient* c) {
    uint8_t chunk[LOG_TX_CHUNK];
    for (int i = 0; i < LOG_TX_CHUNKS_PER_LOOP; i++) {
        uint64_t skipped = 0;
        size_t   n = ring_read(&c->tail, chunk, sizeof(chunk), &skipped);
        if (skipped) {
            c->dropped += (uint32_t)skipped;
            char note[80];
            snprintf(note, sizeof(note),
                     "\r\n<< log: %u bytes dropped (client too slow) >>\r\n", (unsigned)skipped);
            client_note(c->fd, note);
        }
        if (n == 0) return;                        // caught up

        int sent = send(c->fd, chunk, n, MSG_DONTWAIT);
        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                c->tail -= n;                      // un-consume; retry next loop
                return;
            }
            client_close(c);
            return;
        }
        if ((size_t)sent < n) {
            c->tail -= (n - (size_t)sent);         // partial write; resume there
            return;
        }
    }
}

void log_tcp_loop(void) {
    clients_init_once();
    if (s_listen_fd < 0) return;

    accept_pending();

    for (int i = 0; i < LOG_MAX_CLIENTS; i++) {
        LogClient* c = &s_clients[i];
        if (c->fd < 0) continue;
        if (!drain_rx(c)) {
            client_close(c);
            logStream.printf("LOG: monitor disconnected\n");
            continue;
        }
        flush_client(c);
    }
}
