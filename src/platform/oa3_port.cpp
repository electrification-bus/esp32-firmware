// The ESP32 port of cpp-oa3's VenClient: an HTTP(S) transport over HTTPClient, a clock over
// the NTP time network.cpp keeps, a log sink to Serial, the client's storage, and its task.
// See cpp-oa3's doc/porting.md for the contract each piece implements.

#ifdef EBUS_OA3

#include <platform/oa3_port.h>
#include <platform/config.h>
#include <platform/network.h>
#include <platform/tls_trust.h>
#include <peripheral/display.h>

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <MQTTClient.h>
#include <mbedtls/x509.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <new>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <oa3/client/ven_client.h>
#include <oa3/client/mqtt_session.h>
#include <oa3/model/intervals.h>

#ifndef OA3_TASK_STACK
#define OA3_TASK_STACK 8192   // porting.md: at least 6 KB; the spike bottomed at 2.6 KB free of 8
#endif
// The parse arena. One price-server event peaked at 6,984 bytes of it, so without PSRAM it
// is halved from cpp-oa3's 16 KB default to leave internal RAM to the TLS session.
#ifndef OA3_PORT_ARENA_SIZE
#ifdef BOARD_HAS_PSRAM
#define OA3_PORT_ARENA_SIZE OA3_JSON_ARENA_SIZE
#else
#define OA3_PORT_ARENA_SIZE 8192
#endif
#endif
#ifndef OA3_CA_MAX
#define OA3_CA_MAX 4096       // one PEM root
#endif
#ifndef OA3_MQTT_READ_BUF
#define OA3_MQTT_READ_BUF 8192   // a price-server notification is about 3.5 KB (porting.md)
#endif

// gettimeofday() before NTP has synchronized reads 1970; anything before this is unset.
#define OA3_CLOCK_FLOOR_S 1577836800LL   // 2020-01-01T00:00:00Z

namespace {

// The internal-heap low during one operation (an HTTPS request, an MQTT connect), from
// ESP-IDF's local minimum monitor, with the largest internal block just before it. The
// lifetime minimum in the poll line can only fall, so it cannot tell the two apart.
class LocalLow {
public:
    explicit LocalLow(const char* what, const char* host) : _what(what), _host(host) {
        _largest = heap_caps_get_largest_free_block(kCaps);
        _monitoring = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
    }
    ~LocalLow() {
        const size_t low = heap_caps_get_minimum_free_size(kCaps);
        if (_monitoring) heap_caps_monitor_local_minimum_free_size_stop();
        Serial.printf("OA3: %s %s: internal low-water %u, largest block before %u\n", _what,
                      _host, (unsigned)low, (unsigned)_largest);
    }

private:
    static constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const char* _what;
    const char* _host;
    size_t _largest;
    bool _monitoring;
};

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
    bool trust_public_roots() { return tls_trust_public_roots(_secure); }
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
        LocalLow low("GET", _host);

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

// The PEM root for host, from /certs/ under mqtt_build_cert_path()'s name.
bool read_ca(const char* host, char* buf, size_t len) {
    char path[96];
    mqtt_build_cert_path(host, path, sizeof(path));
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("OA3: no CA certificate for %s at %s; verifying it against the public "
                      "root CAs\n", host, path);
        return false;
    }
    size_t n = f.size();
    if (n >= len) {
        Serial.printf("OA3: **ERROR -- %s is %u bytes, more than %u\n", path, (unsigned)n,
                      (unsigned)len - 1);
        f.close();
        return false;
    }
    f.readBytes(buf, n);
    buf[n] = '\0';
    f.close();
    Serial.printf("OA3: CA certificate %s\n", path);
    return true;
}

// One MQTT 3.1.1 session over arduino-mqtt, driven from the OA3 task (porting.md, MqttSession).
// The broker's CA comes from /certs/<host>.ca, named by mqtt_build_cert_path() as the VTN's
// is, else the public root CAs, unless the client passes one. disconnect() always stops the network client, which
// frees the TLS session: MQTTClient::disconnect() returns early once the link is gone.
class ArduinoMqttSession final : public oa3::MqttSession {
public:
    oa3::MqttResult connect(const oa3::MqttConnectOptions& o, bool* session_present) override {
        disconnect();
        if (!o.host || !o.host[0] || !o.client_id || !o.client_id[0]) {
            return oa3::MqttResult::kInvalidArgument;
        }
        if (strlen(o.host) >= sizeof(_host)) return oa3::MqttResult::kInvalidArgument;
        strcpy(_host, o.host);   // MQTTClient keeps the pointer it is given
        if (!network_dns_prime(_host)) return oa3::MqttResult::kDnsFailed;

        Client* net = &_plain;
        if (o.tls) {
            if (!o.verify) {
                _secure.setInsecure();
            } else if (o.ca_pem) {
                _secure.setCACert(o.ca_pem);
            } else if (load_ca_for(_host)) {
                _secure.setCACert(_ca);
            } else if (!tls_trust_public_roots(_secure)) {
                return oa3::MqttResult::kTlsVerifyFailed;
            }
            _secure.setHandshakeTimeout((o.timeout_ms + 999) / 1000);
            net = &_secure;
        }
        LocalLow low("MQTT connect", _host);
        MQTTClient& m = mqtt();
        m.begin(_host, o.port, *net);
        m.onMessageAdvanced(on_mqtt_message);   // begin() clears it
        m.setKeepAlive(o.keepalive_s);
        m.setCleanSession(o.clean_session);
        m.setTimeout(o.timeout_ms);
        if (!m.connect(o.client_id)) {
            const int rc = m.returnCode();
            const lwmqtt_err_t err = m.lastError();
            char text[96] = "";
            const int tls = o.tls ? _secure.lastError(text, sizeof(text)) : 0;
            Serial.printf("OA3: MQTT connect to %s:%u failed (lwmqtt %d, return code %d%s%s)\n",
                          _host, (unsigned)o.port, (int)err, rc, tls < -1 ? ", TLS: " : "",
                          tls < -1 ? text : "");
            disconnect();
            if (rc >= 1 && rc <= 3) return oa3::MqttResult::kRefused;
            if (rc == 4 || rc == 5) return oa3::MqttResult::kNotAuthorized;
            if (err == LWMQTT_NETWORK_TIMEOUT) return oa3::MqttResult::kTimeout;
            if (err == LWMQTT_NETWORK_FAILED_CONNECT) {
                if (tls == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                    return oa3::MqttResult::kTlsVerifyFailed;
                }
                return tls < -1 ? oa3::MqttResult::kTlsFailed : oa3::MqttResult::kConnectFailed;
            }
            return oa3::MqttResult::kIoError;
        }
        if (session_present) *session_present = m.sessionPresent();
        return oa3::MqttResult::kOk;
    }

    void disconnect() override {
        if (_mqtt) _mqtt->disconnect();
        _secure.stop();
        _plain.stop();
    }

    oa3::MqttResult subscribe(const char* filter, uint8_t qos, uint32_t timeout_ms) override {
        if (!connected()) return oa3::MqttResult::kNotConnected;
        mqtt().setTimeout(timeout_ms);
        if (mqtt().subscribe(filter, qos)) return oa3::MqttResult::kOk;
        return mqtt().lastError() == LWMQTT_FAILED_SUBSCRIPTION ? oa3::MqttResult::kSubscribeFailed
                                                                : oa3::MqttResult::kIoError;
    }

    oa3::MqttResult unsubscribe(const char* filter, uint32_t timeout_ms) override {
        if (!connected()) return oa3::MqttResult::kNotConnected;
        mqtt().setTimeout(timeout_ms);
        return mqtt().unsubscribe(filter) ? oa3::MqttResult::kOk : oa3::MqttResult::kIoError;
    }

    oa3::MqttResult poll(uint32_t max_wait_ms, oa3::MqttMessageFn on_message, void* ctx) override {
        (void)max_wait_ms;   // the client passes 0; loop() handles what has arrived
        if (!_mqtt || !_mqtt->connected()) return oa3::MqttResult::kConnectionLost;
        _on_message = on_message;
        _ctx = ctx;
        const bool ok = _mqtt->loop();
        _on_message = nullptr;
        return ok && _mqtt->connected() ? oa3::MqttResult::kOk : oa3::MqttResult::kConnectionLost;
    }

    bool connected() const override { return _mqtt && _mqtt->connected(); }

private:
    // Constructed on first use: its buffers are allocated then (in PSRAM above 4 KB where the
    // board has it), so a build that never connects pays nothing.
    MQTTClient& mqtt() {
        if (!_mqtt) {
            static MQTTClient m(OA3_MQTT_READ_BUF, 512);
            _mqtt = &m;
            m.ref = this;
        }
        return *_mqtt;
    }

    static void on_mqtt_message(MQTTClient* client, char topic[], char bytes[], int length) {
        auto* self = static_cast<ArduinoMqttSession*>(client->ref);
        if (!self || !self->_on_message) return;
        oa3::MqttMessage msg{topic, bytes, static_cast<size_t>(length), 1, false, false};
        self->_on_message(self->_ctx, msg);
    }

    bool load_ca_for(const char* host) { return read_ca(host, _ca, sizeof(_ca)); }

    NetworkClientSecure _secure;
    NetworkClient _plain;
    MQTTClient* _mqtt = nullptr;
    oa3::MqttMessageFn _on_message = nullptr;
    void* _ctx = nullptr;
    char _host[128];
    char _ca[OA3_CA_MAX];
};

ArduinoHttpTransport _http;
ArduinoMqttSession _mqtt_session;
SntpClock _clock;
char _ca[OA3_CA_MAX];
char _ca_host[128];
oa3::JsonArena* _arena = nullptr;
uint32_t _logged_requests = 0;

// The price screen, on the display oa3.display names (doc/OpenADR3.md). Drawn only from the
// OA3 task: at its start, and from the client's callbacks, which run inside loop().
Display* _display = nullptr;
oa3::VenClient* _client = nullptr;   // for the program's name in the header
bool _header_named = false;

// A UTC instant as local time in device.timezone, e.g. "11:00 PST".
void hhmm(const oa3::DateTime& t, char* out, size_t len) {
    const time_t secs = static_cast<time_t>(t.seconds);
    struct tm local;
    if (!localtime_r(&secs, &local) || strftime(out, len, "%H:%M %Z", &local) == 0) {
        snprintf(out, len, "?");
    }
}

// "USD", "KWH" -> "$/kWh". The VTN's codes are upper case; the panel's built-in font has no
// currency signs beyond "$", so other currencies keep their codes.
void unit_label(const char* currency, const char* units, char* out, size_t len) {
    const char* cur = strcmp(currency, "USD") == 0 ? "$" : currency;
    const char* u = units;
    if (strcmp(units, "KWH") == 0) u = "kWh";
    else if (strcmp(units, "MWH") == 0) u = "MWh";
    else if (strcmp(units, "WH") == 0) u = "Wh";
    if (cur[0] && u[0]) snprintf(out, len, "%s/%s", cur, u);
    else snprintf(out, len, "%s%s", cur, u);
}

// The header is the program: its configured name from the start, or, when only
// oa3.program_id is configured, the id until the client has resolved the name.
void screen_header() {
    if (_header_named) return;
    const char* name = oa3_program_name;
    if (!name[0]) {
        const oa3::Program* program = _client ? _client->program() : nullptr;
        name = program && program->program_name[0] ? program->program_name : oa3_program_id;
        _header_named = program != nullptr;
    } else {
        _header_named = true;
    }
    _display->fill_rect(0, 0, _display->width(), 30, Display::kBlack);
    _display->text(8, 8, name, 2, Display::kWhite, Display::kBlack);
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
    screen_header();
    _display->fill_rect(0, 30, _display->width(), 1, Display::kGrey);
    _display->text(8, 90, "waiting for a price", 2, Display::kWhite, Display::kBlack);
    _display->flush();
}

#ifndef OA3_CHART_HOURS
#define OA3_CHART_HOURS 12    // the price screen's chart: hours ahead of now
#endif
#ifndef OA3_CHART_STEPS
#define OA3_CHART_STEPS 32    // spans it draws; the price server's are an hour each
#endif

constexpr int16_t kChartBottom = 200;   // the status line's band starts here
int16_t _chart_top = -1;                // where the price screen put the chart; -1 for none

// The x of instant t on a chart of [now, now + span_s) drawn width pixels wide from left.
int16_t chart_x(int64_t t, int64_t now, int64_t span_s, int16_t left, int16_t width) {
    return static_cast<int16_t>(left + (t - now) * width / span_s);
}

// The next OA3_CHART_HOURS of prices as bars from y top to bottom: the span in force in white,
// later ones in blue, none where no price is in force. Bars are scaled from the lowest price
// (a stub) to the highest, so the shape shows; both are printed above the bars, and every
// third local hour below them.
void screen_chart(int16_t top, int16_t bottom) {
    const int16_t label_h = _display->char_height(1);
    const int16_t bars_top = top + label_h + 2;
    const int16_t bars_bottom = bottom - label_h - 2;
    const int16_t left = 8;
    const int16_t width = _display->width() - 2 * left;
    _display->fill_rect(0, top, _display->width(), bottom - top, Display::kBlack);
    if (!_client || bars_bottom - bars_top < 20) return;

    const int64_t now = static_cast<int64_t>(time(nullptr));
    const int64_t span_s = static_cast<int64_t>(OA3_CHART_HOURS) * 3600;
    oa3::TimeInterval window{};
    if (!oa3::from_unix(now, &window.start) || !oa3::from_unix(now + span_s, &window.end)) return;
    static oa3::PriceStep steps[OA3_CHART_STEPS];
    // "PRICE": the payload type VenConfig follows by default, as this port leaves it.
    const size_t n = oa3::price_schedule(_client->events(), _client->event_count(), window,
                                         "PRICE", steps, OA3_CHART_STEPS, nullptr);
    double lo = 0, hi = 0;
    bool any = false;
    for (size_t i = 0; i < n; ++i) {
        if (!steps[i].value) continue;
        const double v = steps[i].value->number;
        if (!any || v > hi) hi = v;
        if (!any || v < lo) lo = v;
        any = true;
    }
    if (!any) return;

    const int16_t stub = 4;   // the lowest price's bar
    const int16_t bars_h = bars_bottom - bars_top - stub;
    for (size_t i = 0; i < n; ++i) {
        if (!steps[i].value) continue;
        const int16_t x0 = chart_x(oa3::to_unix(steps[i].span.start), now, span_s, left, width);
        const int16_t x1 = chart_x(oa3::to_unix(steps[i].span.end), now, span_s, left, width);
        const double frac = hi > lo ? (steps[i].value->number - lo) / (hi - lo) : 1.0;
        const int16_t h = stub + static_cast<int16_t>(frac * bars_h);
        const int16_t w = x1 - x0 > 2 ? x1 - x0 - 1 : 1;
        _display->fill_rect(x0, bars_bottom - h, w, h, i == 0 ? Display::kWhite : Display::kBlue);
    }

    char label[40];
    snprintf(label, sizeof(label), "max %.4g   min %.4g", hi, lo);
    _display->text(left, top, label, 1, Display::kGrey, Display::kBlack);
    const int16_t hour_w = 2 * _display->char_width(1);
    for (int64_t t = (now / 3600 + 1) * 3600; t < now + span_s; t += 3600) {
        const time_t secs = static_cast<time_t>(t);
        struct tm local;
        if (!localtime_r(&secs, &local) || local.tm_hour % 3 != 0) continue;
        const int16_t x = chart_x(t, now, span_s, left, width);
        if (x + hour_w > left + width) continue;
        snprintf(label, sizeof(label), "%02d", local.tm_hour);
        _display->text(x, bars_bottom + 2, label, 1, Display::kGrey, Display::kBlack);
    }
}

void screen_price(const oa3::PriceInfo& p) {
    if (!_display) return;
    PeripheralLock l(*_display);
    screen_header();
    _display->fill_rect(0, 40, _display->width(), 150, Display::kBlack);
    _chart_top = -1;
    if (!p.active) {
        _display->text(8, 90, "no price in force", 2, Display::kYellow, Display::kBlack);
        _display->flush();
        return;
    }
    char line[48];
    _display->text(8, 50, p.text, 5, Display::kWhite, Display::kBlack);
    // Units beside the price, bottom-aligned with it, where they fit; else on the line below.
    unit_label(p.currency, p.units, line, sizeof(line));
    const int16_t price_right = 8 + static_cast<int16_t>(strlen(p.text)) * _display->char_width(5);
    const int16_t units_w = static_cast<int16_t>(strlen(line)) * _display->char_width(2);
    int16_t next_y = 105;
    if (price_right + 6 + units_w <= _display->width() - 4) {
        _display->text(price_right + 6, 50 + _display->char_height(5) - _display->char_height(2),
                       line, 2, Display::kWhite, Display::kBlack);
    } else {
        _display->text(8, 105, line, 2, Display::kWhite, Display::kBlack);
        next_y = 140;
    }
    char next[24];
    if (p.has_next_change) {
        hhmm(p.next_change, next, sizeof(next));
    } else if (!p.span.end_indefinite) {
        hhmm(p.span.end, next, sizeof(next));
    } else {
        snprintf(next, sizeof(next), "none");
    }
    snprintf(line, sizeof(line), "next change %s", next);
    _display->text(8, next_y, line, 2, Display::kWhite, Display::kBlack);
    _chart_top = next_y + _display->char_height(2) + 6;
    screen_chart(_chart_top, kChartBottom);
    _display->flush();
}

// Redraws the chart alone: the events can change, by a fetch or a notification, without
// the price in force changing, and the client reports only the latter.
void screen_chart_refresh() {
    if (!_display || _chart_top < 0) return;
    PeripheralLock l(*_display);
    screen_chart(_chart_top, kChartBottom);
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
    Serial.printf("OA3: status %s (http %s, %d; %u failures; retry in %lu s)",
                  oa3::to_string(s.status), oa3::to_string(s.http_result), s.http_status,
                  (unsigned)s.consecutive_failures, (unsigned long)(s.retry_in_ms / 1000));
    if (s.notify != oa3::NotifyState::kOff) {
        Serial.printf("; notify %s (mqtt %s; %u failures; %lu notifications)",
                      oa3::to_string(s.notify), oa3::to_string(s.mqtt_result),
                      (unsigned)s.mqtt_failures, (unsigned long)s.notifications);
    }
    Serial.println();
}

// After each request: the memory the v0.1.0 soak watches for drift.
void log_poll(const oa3::VenClient& client) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    Serial.printf("OA3: poll %s, notify %s, %u events; internal free %u, largest %u, min %u; "
                  "task stack free %u; arena high water %u of %u\n",
                  oa3::to_string(client.status().status), oa3::to_string(client.status().notify),
                  (unsigned)client.event_count(),
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
    uint32_t charted_notifications = 0;
    for (;;) {
        client->loop();
        const bool fetched = _http.requests() != _logged_requests;
        if (fetched) {
            _logged_requests = _http.requests();
            log_poll(*client);
        }
        if (fetched || client->status().notifications != charted_notifications) {
            charted_notifications = client->status().notifications;
            screen_chart_refresh();
        }
        uint32_t wait = client->ms_until_due();
        if (wait > 1000) wait = 1000;
        vTaskDelay(pdMS_TO_TICKS(wait) + 1);
    }
}

// The PEM root for base_url's host.
bool load_ca(const char* base_url) {
    const char* host = strstr(base_url, "://");
    host = host ? host + 3 : base_url;
    size_t len = strcspn(host, ":/");
    if (len == 0 || len >= sizeof(_ca_host)) return false;
    memcpy(_ca_host, host, len);
    _ca_host[len] = '\0';
    return read_ca(_ca_host, _ca, sizeof(_ca));
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
    void* arena_buf = heap_caps_aligned_alloc(8, OA3_PORT_ARENA_SIZE, caps);
    if (!events_mem || !arena_buf) {
        Serial.printf("OA3: **ERROR -- no memory for the client's storage (%u bytes)\n",
                      (unsigned)(2 * n * sizeof(oa3::Event) + OA3_PORT_ARENA_SIZE));
        return false;
    }
    oa3::Event* events = static_cast<oa3::Event*>(events_mem);
    for (size_t i = 0; i < 2 * n; i++) new (&events[i]) oa3::Event();
    static oa3::JsonArena arena(arena_buf, OA3_PORT_ARENA_SIZE);
    *out = oa3::VenStorage{events, events + n, n, &arena};
    if (oa3_notifications) {
        void* notes = heap_caps_aligned_alloc(alignof(oa3::NotificationStorage),
                                              sizeof(oa3::NotificationStorage), caps);
        if (!notes) {
            Serial.printf("OA3: **ERROR -- no memory for the notification storage (%u bytes)\n",
                          (unsigned)sizeof(oa3::NotificationStorage));
            return false;
        }
        out->notifications = new (notes) oa3::NotificationStorage();
    }
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
        // A /certs/<Host>.ca file wins (a private CA); otherwise the public roots.
        if (load_ca(oa3_base_url)) {
            _http.set_ca(_ca);
        } else if (!_http.trust_public_roots()) {
            Serial.println("OA3: **ERROR -- no CA certificate, and no public root CAs on this "
                           "framework");
            return;
        }
    }
    oa3::VenStorage storage;
    if (!make_storage(&storage)) return;
    static oa3::VenClient client(_http, _clock, storage, &_mqtt_session);
    _client = &client;
    static char client_id[48];
    snprintf(client_id, sizeof(client_id), "oa3-%s", getDeviceID());

    oa3::VenConfig c{};
    c.base_url = oa3_base_url;
    if (oa3_program_name[0]) c.program_name = oa3_program_name;
    if (oa3_program_id[0]) c.program_id = oa3_program_id;
    if (oa3_poll_period_s) c.poll_period_s = oa3_poll_period_s;
    if (oa3_notifications) {
        c.notifications = oa3::NotificationMode::kMqtt;
        c.exclusive_tls = oa3_exclusive_tls;
        c.mqtt_client_id = client_id;
    }
    c.on_price_change = on_price;
    c.on_status = on_status;
    c.log = oa3::LogSink{on_log, nullptr, oa3::LogLevel::kInfo};
    if (!client.start(c)) {
        Serial.println("OA3: **ERROR -- the oa3: configuration is not valid; the VEN is not started");
        return;
    }
    Serial.printf("OA3: VEN for %s at %s, up to %u events; notifications %s%s\n",
                  oa3_program_name[0] ? oa3_program_name : oa3_program_id, oa3_base_url,
                  (unsigned)OA3_MAX_EVENTS, oa3_notifications ? "MQTT as " : "off",
                  oa3_notifications ? client_id : "");
    if (oa3_notifications) {
        Serial.printf("OA3: exclusive_tls %s\n", oa3_exclusive_tls ? "on" : "off");
    }
    xTaskCreatePinnedToCore(oa3_task, "oa3", OA3_TASK_STACK, &client, 1, nullptr, 1);
}

#endif  // EBUS_OA3
