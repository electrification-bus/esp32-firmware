// The ESP32 port of cpp-oa3's VenClient: an HTTP(S) transport over HTTPClient, a clock over
// the NTP time network.cpp keeps, a log sink to Serial, the client's storage, and its task.
// See cpp-oa3's doc/porting.md for the contract each piece implements.

#ifdef EBUS_OA3

#include <platform/oa3_port.h>
#include <platform/config.h>
#include <platform/network.h>
#include <peripheral/display.h>

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <new>
#include <string.h>
#include <sys/time.h>

#include <oa3/client/ven_client.h>

#ifndef OA3_TASK_STACK
#define OA3_TASK_STACK 8192   // porting.md: at least 6 KB; the spike bottomed at 2.6 KB free of 8
#endif
#ifndef OA3_CA_MAX
#define OA3_CA_MAX 4096       // one PEM root
#endif

// gettimeofday() before NTP has synchronized reads 1970; anything before this is unset.
#define OA3_CLOCK_FLOOR_S 1577836800LL   // 2020-01-01T00:00:00Z

namespace {

// A response body from HTTPClient's stream: Content-Length bytes when the server sent one,
// otherwise until it closes the connection, and never past the request's deadline.
class StreamReader final : public oa3::ByteReader {
public:
    StreamReader(NetworkClient& s, int size, uint32_t deadline_ms)
        : _s(s), _left(size), _deadline(deadline_ms) {}

    int read() override {
        char c;
        return readBytes(&c, 1) == 1 ? static_cast<unsigned char>(c) : -1;
    }

    size_t readBytes(char* buf, size_t len) override {
        size_t n = 0;
        while (n < len && _left != 0) {
            if (static_cast<int32_t>(millis() - _deadline) >= 0) {
                timed_out = true;
                break;
            }
            const int avail = _s.available();
            if (avail > 0) {
                size_t want = len - n;
                if (_left > 0 && want > static_cast<size_t>(_left)) want = _left;
                const int got = _s.read(reinterpret_cast<uint8_t*>(buf + n), want);
                if (got > 0) {
                    n += got;
                    if (_left > 0) _left -= got;
                }
            } else if (!_s.connected()) {
                if (_left > 0) cut_short = true;
                break;
            } else {
                delay(1);
            }
        }
        return n;
    }

    bool timed_out = false;
    bool cut_short = false;   // the connection closed before Content-Length bytes arrived

private:
    NetworkClient& _s;
    int _left;  // -1: until the connection closes
    uint32_t _deadline;
};

// One GET per call; the connection and its TLS session are closed before get() returns, so
// the client never holds one between requests (a second does not fit on the ESP32).
class ArduinoHttpTransport final : public oa3::HttpTransport {
public:
    void set_ca(const char* pem) { _secure.setCACert(pem); }
    uint32_t requests() const { return _requests; }

    oa3::HttpResult get(const oa3::HttpRequest& req, oa3::HttpResponseFn on_response,
                        void* ctx) override {
        _requests++;
        const bool https = strncmp(req.url, "https://", 8) == 0;
        if (!https && strncmp(req.url, "http://", 7) != 0) return oa3::HttpResult::kInvalidUrl;
        NetworkClient& client = https ? static_cast<NetworkClient&>(_secure) : _plain;

        const uint32_t timeout_ms = req.timeout_ms ? req.timeout_ms : 15000;
        const uint32_t deadline = millis() + timeout_ms;
        if (!host_of(req.url)) return oa3::HttpResult::kInvalidUrl;
        if (!network_dns_prime(_host)) return oa3::HttpResult::kDnsFailed;

        // Each phase gets what is left of the one budget. The header read is bounded per
        // gap between lines (HTTPClient's setTimeout), not in total.
        const uint32_t left = remaining(deadline);
        if (left == 0) return oa3::HttpResult::kTimeout;
        if (https) _secure.setHandshakeTimeout((left + 999) / 1000);
        _http.useHTTP10(true);   // no chunked body, and no keep-alive
        _http.setConnectTimeout(left);
        _http.setTimeout(left > 65535 ? 65535 : left);
        if (!_http.begin(client, req.url)) return oa3::HttpResult::kInvalidUrl;
        for (size_t i = 0; i < req.header_count; i++) {
            _http.addHeader(req.headers[i].name, req.headers[i].value);
        }

        const int code = _http.GET();
        if (code < 0) {
            // A refused connection is a TCP or TLS failure (DNS was checked above). The
            // TLS layer stores -1 for a TCP failure or a handshake timeout, and the mbedTLS
            // code (below -1) for a handshake or certificate failure.
            char err[96] = "";
            const int tls = https ? _secure.lastError(err, sizeof(err)) : 0;
            const bool tls_failed = code == HTTPC_ERROR_CONNECTION_REFUSED && tls < -1;
            Serial.printf("OA3: GET %s failed: %s%s%s\n", _host,
                          HTTPClient::errorToString(code).c_str(), tls_failed ? ", TLS: " : "",
                          tls_failed ? err : "");
            _http.end();
            if (code == HTTPC_ERROR_READ_TIMEOUT || remaining(deadline) == 0) {
                return oa3::HttpResult::kTimeout;
            }
            if (tls_failed) return oa3::HttpResult::kTlsFailed;
            if (code == HTTPC_ERROR_CONNECTION_REFUSED) return oa3::HttpResult::kConnectFailed;
            return oa3::HttpResult::kIoError;
        }

        StreamReader body(_http.getStream(), _http.getSize(), deadline);
        on_response(ctx, code, body);
        _http.end();
        if (body.timed_out) return oa3::HttpResult::kTimeout;
        if (body.cut_short) return oa3::HttpResult::kIoError;
        return oa3::HttpResult::kOk;
    }

private:
    static uint32_t remaining(uint32_t deadline) {
        const int32_t left = static_cast<int32_t>(deadline - millis());
        return left > 0 ? static_cast<uint32_t>(left) : 0;
    }

    // The URL's host into _host, for network_dns_prime().
    bool host_of(const char* url) {
        const char* host = strstr(url, "://");
        if (!host) return false;
        host += 3;
        const size_t len = strcspn(host, ":/?#");
        if (len == 0 || len >= sizeof(_host)) return false;
        memcpy(_host, host, len);
        _host[len] = '\0';
        return true;
    }

    NetworkClientSecure _secure;
    NetworkClient _plain;
    HTTPClient _http;
    char _host[128];
    uint32_t _requests = 0;
};

// UTC from gettimeofday(), set by the firmware's NTP client (network.cpp).
class SntpClock final : public oa3::Clock {
public:
    bool now(oa3::DateTime* out) override {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        if (tv.tv_sec < OA3_CLOCK_FLOOR_S) return false;
        return oa3::from_unix(tv.tv_sec, static_cast<uint32_t>(tv.tv_usec) * 1000, out);
    }
    uint32_t monotonic_ms() override { return millis(); }
};

ArduinoHttpTransport _http;
SntpClock _clock;
char _ca[OA3_CA_MAX];
char _ca_host[128];
oa3::JsonArena* _arena = nullptr;
uint32_t _logged_requests = 0;

// The price screen, on the display oa3.display names (doc/OpenADR3.md). Drawn only from the
// OA3 task: at its start, and from the client's callbacks, which run inside loop().
Display* _display = nullptr;

// "2026-11-13T19:00:00Z" -> "19:00 UTC"
void hhmm(const oa3::DateTime& t, char* out, size_t len) {
    char iso[oa3::kDateTimeStringSize];
    if (oa3::format_datetime(t, iso, sizeof(iso)) >= 16) {
        snprintf(out, len, "%.5s UTC", iso + 11);
    } else {
        snprintf(out, len, "?");
    }
}

void screen_begin() {
    if (oa3_display[0] == '\0') return;
    _display = peripheral<Display>(oa3_display);
    if (!_display) {
        Serial.printf("OA3: **ERROR -- oa3.display '%s' is not a display peripheral\n", oa3_display);
        return;
    }
    PeripheralLock l(*_display);
    _display->clear(Display::kBlack);
    _display->text(8, 8, "OpenADR 3 price", 2, Display::kWhite, Display::kBlack);
    _display->fill_rect(0, 30, _display->width(), 1, Display::kGrey);
    _display->text(8, 90, "waiting for a price", 2, Display::kWhite, Display::kBlack);
    _display->flush();
}

void screen_price(const oa3::PriceInfo& p) {
    if (!_display) return;
    PeripheralLock l(*_display);
    _display->fill_rect(0, 40, _display->width(), 150, Display::kBlack);
    if (!p.active) {
        _display->text(8, 90, "no price in force", 2, Display::kYellow, Display::kBlack);
        _display->flush();
        return;
    }
    char line[48];
    _display->text(8, 50, p.text, 5, Display::kWhite, Display::kBlack);
    snprintf(line, sizeof(line), "%s/%s", p.currency, p.units);
    _display->text(8, 105, line, 2, Display::kWhite, Display::kBlack);
    char next[24];
    if (p.has_next_change) {
        hhmm(p.next_change, next, sizeof(next));
    } else if (!p.span.end_indefinite) {
        hhmm(p.span.end, next, sizeof(next));
    } else {
        snprintf(next, sizeof(next), "none");
    }
    snprintf(line, sizeof(line), "next change %s", next);
    _display->text(8, 140, line, 2, Display::kWhite, Display::kBlack);
    _display->flush();
}

void screen_status(const oa3::VenStatusInfo& s) {
    if (!_display) return;
    uint16_t color = Display::kRed;
    if (s.status == oa3::VenStatus::kOk) {
        color = Display::kGreen;
    } else if (s.status == oa3::VenStatus::kWaitingForClock ||
               s.status == oa3::VenStatus::kResolvingProgram ||
               s.status == oa3::VenStatus::kStale || s.status == oa3::VenStatus::kTruncated) {
        color = Display::kYellow;
    }
    PeripheralLock l(*_display);
    _display->fill_rect(0, 200, _display->width(), 40, Display::kBlack);
    _display->text(8, 210, oa3::to_string(s.status), 2, color, Display::kBlack);
    _display->flush();
}

void on_log(void*, oa3::LogLevel level, const char* line) {
    Serial.printf("OA3: %s %s\n", oa3::to_string(level), line);
}

void on_price(void*, const oa3::PriceInfo& p) {
    screen_price(p);
    if (!p.active) {
        Serial.println("OA3: no price in force");
        return;
    }
    char start[oa3::kDateTimeStringSize], end[oa3::kDateTimeStringSize];
    oa3::format_datetime(p.span.start, start, sizeof(start));
    if (p.span.end_indefinite) {
        strcpy(end, "indefinite");
    } else {
        oa3::format_datetime(p.span.end, end, sizeof(end));
    }
    Serial.printf("OA3: price %s %s/%s, interval %ld of event %s, %s to %s\n", p.text,
                  p.currency, p.units, (long)p.interval_id, p.event_id, start, end);
}

void on_status(void*, const oa3::VenStatusInfo& s) {
    screen_status(s);
    Serial.printf("OA3: status %s (http %s, %d; %u failures; retry in %lu s)\n",
                  oa3::to_string(s.status), oa3::to_string(s.http_result), s.http_status,
                  (unsigned)s.consecutive_failures, (unsigned long)(s.retry_in_ms / 1000));
}

// After each request: the memory the v0.1.0 soak watches for drift.
void log_poll(const oa3::VenClient& client) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Serial.printf("OA3: poll %s, %u events; internal free %u, largest %u, min %u; "
                  "task stack free %u; arena high water %u of %u\n",
                  oa3::to_string(client.status().status), (unsigned)client.event_count(),
                  (unsigned)heap_caps_get_free_size(caps),
                  (unsigned)heap_caps_get_largest_free_block(caps),
                  (unsigned)heap_caps_get_minimum_free_size(caps),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                  (unsigned)(_arena ? _arena->high_water() : 0),
                  (unsigned)(_arena ? _arena->capacity() : 0));
}

void oa3_task(void* arg) {
    oa3::VenClient* client = static_cast<oa3::VenClient*>(arg);
    screen_begin();
    for (;;) {
        client->loop();
        if (_http.requests() != _logged_requests) {
            _logged_requests = _http.requests();
            log_poll(*client);
        }
        uint32_t wait = client->ms_until_due();
        if (wait > 1000) wait = 1000;
        vTaskDelay(pdMS_TO_TICKS(wait) + 1);
    }
}

// The PEM root for base_url's host, from /certs/ under mqtt_build_cert_path()'s name.
bool load_ca(const char* base_url) {
    const char* host = strstr(base_url, "://");
    host = host ? host + 3 : base_url;
    size_t len = strcspn(host, ":/");
    if (len == 0 || len >= sizeof(_ca_host)) return false;
    memcpy(_ca_host, host, len);
    _ca_host[len] = '\0';

    char path[96];
    mqtt_build_cert_path(_ca_host, path, sizeof(path));
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("OA3: **ERROR -- no CA certificate for %s at %s\n", _ca_host, path);
        return false;
    }
    size_t n = f.size();
    if (n >= sizeof(_ca)) {
        Serial.printf("OA3: **ERROR -- %s is %u bytes, more than %u\n", path, (unsigned)n,
                      (unsigned)sizeof(_ca) - 1);
        f.close();
        return false;
    }
    f.readBytes(_ca, n);
    _ca[n] = '\0';
    f.close();
    Serial.printf("OA3: CA certificate %s\n", path);
    return true;
}

// The client's event arrays and parse arena (about 38 KB with OA3_MAX_EVENTS=2), allocated
// once here and never freed: in PSRAM on a board that has it, so the internal heap is left
// to the TLS session (porting.md); otherwise in internal RAM. Never static: that much .bss
// leaves ESP-IDF too little heap at startup, which aborts ("esp_timer: Not enough memory to
// create timer task") before Arduino marks a new image valid, so OTA rolls it back.
bool make_storage(oa3::VenStorage* out) {
#ifdef BOARD_HAS_PSRAM
    const uint32_t caps = MALLOC_CAP_SPIRAM;
#else
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
    const size_t n = OA3_MAX_EVENTS;
    void* events_mem = heap_caps_aligned_alloc(alignof(oa3::Event), 2 * n * sizeof(oa3::Event),
                                               caps);
    void* arena_buf = heap_caps_aligned_alloc(8, OA3_JSON_ARENA_SIZE, caps);
    if (!events_mem || !arena_buf) {
        Serial.printf("OA3: **ERROR -- no memory for the client's storage (%u bytes)\n",
                      (unsigned)(2 * n * sizeof(oa3::Event) + OA3_JSON_ARENA_SIZE));
        return false;
    }
    oa3::Event* events = static_cast<oa3::Event*>(events_mem);
    for (size_t i = 0; i < 2 * n; i++) new (&events[i]) oa3::Event();
    static oa3::JsonArena arena(arena_buf, OA3_JSON_ARENA_SIZE);
    *out = oa3::VenStorage{events, events + n, n, &arena};
    _arena = out->arena;
    return true;
}

}  // namespace

void setup_oa3() {
    if (oa3_base_url[0] == '\0') {
        Serial.println("OA3: **ERROR -- oa3.base_url is not set; the VEN is not started");
        return;
    }
    if (strncmp(oa3_base_url, "https://", 8) == 0) {
        if (!load_ca(oa3_base_url)) return;
        _http.set_ca(_ca);
    }
    oa3::VenStorage storage;
    if (!make_storage(&storage)) return;
    static oa3::VenClient client(_http, _clock, storage);

    oa3::VenConfig c{};
    c.base_url = oa3_base_url;
    if (oa3_program_name[0]) c.program_name = oa3_program_name;
    if (oa3_program_id[0]) c.program_id = oa3_program_id;
    if (oa3_poll_period_s) c.poll_period_s = oa3_poll_period_s;
    c.on_price_change = on_price;
    c.on_status = on_status;
    c.log = oa3::LogSink{on_log, nullptr, oa3::LogLevel::kInfo};
    if (!client.start(c)) {
        Serial.println("OA3: **ERROR -- the oa3: configuration is not valid; the VEN is not started");
        return;
    }
    Serial.printf("OA3: VEN for %s at %s, up to %u events\n",
                  oa3_program_name[0] ? oa3_program_name : oa3_program_id, oa3_base_url,
                  (unsigned)OA3_MAX_EVENTS);
    xTaskCreatePinnedToCore(oa3_task, "oa3", OA3_TASK_STACK, &client, 1, nullptr, 1);
}

#endif  // EBUS_OA3
