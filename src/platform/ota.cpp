#include <platform/ota.h>
#include <platform/config.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>

// A PEM CA is ~2KB. Allocated only for the duration of an https pull and freed straight
// after, rather than held as static: the static footprint is already near the level that
// starves the FreeRTOS idle task at boot (see the note in Device.cpp).
#define OTA_CA_MAX_SIZE 4096
// Abandon a download that has delivered no data for this long while still connected.
#define OTA_STALL_TIMEOUT_MS 30000

static bool      _ota_pending      = false;
static bool      _fs_pending       = false;
static bool      _armed            = false;
static OtaStatus _ota_status       = OTA_STATUS_IDLE;
// Each target keeps its own URL, captured when it is queued: staging both images must
// not leave the filesystem step downloading the firmware URL. _last_url is the one most
// recently queued or downloading, for status reporting. All sized to the config source.
static char      _fw_url[128]      = {0};
static char      _fs_url[128]      = {0};
static char      _last_url[128]    = {0};
static char      _last_host[128]   = {0};
static char      _last_file[128]   = {0};
static int       _last_http_code   = 0;

static void parse_url(const char* url) {
    _last_host[0] = '\0';
    _last_file[0] = '\0';
    const char* p = strstr(url, "://");
    if (!p) return;
    p += 3;
    const char* slash = strchr(p, '/');
    if (!slash) {
        strncpy(_last_host, p, sizeof(_last_host) - 1);
        strncpy(_last_file, "/", sizeof(_last_file) - 1);
        return;
    }
    size_t host_len = (size_t)(slash - p);
    if (host_len >= sizeof(_last_host)) host_len = sizeof(_last_host) - 1;
    strncpy(_last_host, p, host_len);
    _last_host[host_len] = '\0';
    strncpy(_last_file, slash, sizeof(_last_file) - 1);
    _last_file[sizeof(_last_file) - 1] = '\0';
}

OtaStatus   ota_get_status()        { return _ota_status; }
const char* ota_get_last_host()     { return _last_host; }
const char* ota_get_last_file()     { return _last_file; }
const char* ota_get_last_url()      { return _last_url; }
int         ota_get_last_http_code(){ return _last_http_code; }

const char* ota_get_status_str() {
    switch (_ota_status) {
        case OTA_STATUS_IDLE:           return "idle";
        case OTA_STATUS_DOWNLOADING:    return "downloading";
        case OTA_STATUS_HOST_NOT_FOUND: return "host_not_found";
        case OTA_STATUS_FILE_NOT_FOUND: return "file_not_found";
        case OTA_STATUS_HTTP_ERROR:     return "http_error";
        case OTA_STATUS_WRITE_ERROR:    return "write_error";
        case OTA_STATUS_DOWNLOAD_INCOMPLETE: return "download_incomplete";
        case OTA_STATUS_COMPLETE:       return "complete";
        default:                        return "unknown";
    }
}

void ota_arm() {
    if (_ota_pending || _fs_pending) _armed = true;
}

void ota_start_update(bool start_now) {
    if (!ota_is_enabled()) {
        Serial.println("OTA: Update rejected - OTA not enabled");
        return;
    }
    if (firmware_url[0] == '\0') {
        Serial.println("OTA: Update rejected - no firmware URL");
        return;
    }
    strncpy(_fw_url, firmware_url, sizeof(_fw_url) - 1);
    _fw_url[sizeof(_fw_url) - 1] = '\0';
    strncpy(_last_url, _fw_url, sizeof(_last_url) - 1);
    _last_url[sizeof(_last_url) - 1] = '\0';
    parse_url(_last_url);
    _last_http_code = 0;
    _ota_pending = true;
    if (start_now) _armed = true;
    Serial.printf("OTA: Update %s from %s\n", start_now ? "queued" : "staged", _last_url);
}

void ota_start_fs_update(bool start_now) {
    if (!ota_is_enabled()) {
        Serial.println("OTA: FS update rejected - OTA not enabled");
        return;
    }
    if (fs_url[0] == '\0') {
        Serial.println("OTA: FS update rejected - no fs URL");
        return;
    }
    strncpy(_fs_url, fs_url, sizeof(_fs_url) - 1);
    _fs_url[sizeof(_fs_url) - 1] = '\0';
    strncpy(_last_url, _fs_url, sizeof(_last_url) - 1);
    _last_url[sizeof(_last_url) - 1] = '\0';
    parse_url(_last_url);
    _last_http_code = 0;
    _fs_pending = true;
    if (start_now) _armed = true;
    Serial.printf("OTA: FS update %s from %s\n", start_now ? "queued" : "staged", _last_url);
}

static bool url_is_https(const char* url) {
    return strncasecmp(url, "https://", 8) == 0;
}

// Read /certs/<Host>.ca for the OTA host into `buf`, using the same naming scheme as the
// MQTT broker certs (mqtt_build_cert_path). Returns false when there is none — the caller
// then decides whether to proceed unverified. Deliberately NOT mqtt_load_ca_cert(): that
// resolves against mqtt_host and caches into the shared MQTT cert buffer, so reusing it
// here would serve the wrong cert and evict the broker's.
static bool load_ota_ca_cert(const char* host, char* buf, size_t buf_len) {
    char cert_path[96];
    mqtt_build_cert_path(host, cert_path, sizeof(cert_path));
    if (!LittleFS.begin(false)) return false;
    if (!LittleFS.exists(cert_path)) {
        Serial.printf("OTA: No CA cert at '%s'\n", cert_path);
        return false;
    }
    File f = LittleFS.open(cert_path, FILE_READ);
    if (!f) return false;
    size_t n = f.size();
    if (n >= buf_len) {
        Serial.printf("OTA: CA cert too large (%u bytes, max %u)\n",
                      (unsigned)n, (unsigned)buf_len - 1);
        f.close();
        return false;
    }
    f.readBytes(buf, n);
    buf[n] = '\0';
    f.close();
    Serial.printf("OTA: Using CA cert '%s'\n", cert_path);
    return true;
}

static bool run_update(int update_type, const char* url) {
    HTTPClient http;
    WiFiClient plain_client;
    WiFiClientSecure secure_client;
    char* ca_cert = nullptr;

    strncpy(_last_url, url, sizeof(_last_url) - 1);
    _last_url[sizeof(_last_url) - 1] = '\0';
    parse_url(_last_url);
    _last_http_code = 0;
    bool  https   = url_is_https(_last_url);

    Serial.printf("OTA: Starting %s update from %s\n",
                  update_type == U_SPIFFS ? "filesystem" : "firmware", _last_url);
    _ota_status = OTA_STATUS_DOWNLOADING;

    WiFiClient* client = &plain_client;
    if (https) {
        // TLS costs ~40KB of heap for the session, on top of whatever the MQTT transport
        // is already holding. Log the headroom so a failure here is diagnosable.
        Serial.printf("OTA: https — free heap before TLS: %u bytes\n", ESP.getFreeHeap());
        ca_cert = new char[OTA_CA_MAX_SIZE];
        if (ca_cert && load_ota_ca_cert(_last_host, ca_cert, OTA_CA_MAX_SIZE)) {
            secure_client.setCACert(ca_cert);
        } else {
            // Encrypted but UNAUTHENTICATED: anything that can intercept the connection
            // can serve its own image. Put a cert at /certs/<Host>.ca (data/certs/, then
            // flash the filesystem) to verify the server.
            Serial.println("OTA: WARNING - no CA cert for this host, not verifying the "
                           "server. Place one in data/certs/ to fix.");
            secure_client.setInsecure();
        }
        secure_client.setTimeout(15);
        client = &secure_client;
    }

    if (!http.begin(*client, _last_url)) {
        Serial.println("OTA: Failed to connect to server");
        _ota_status = OTA_STATUS_HOST_NOT_FOUND;
        delete[] ca_cert;
        return false;
    }

    // Release hosting (GitHub, S3, most CDNs) answers a download URL with a 30x to a
    // signed location. HTTPClient does not follow those unless told to; without this an
    // ordinary release URL fails as http_error. STRICT = GET/HEAD only, which is all a
    // pull does. NB a plain-http client still cannot follow a redirect TO https.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();
    _last_http_code = httpCode;

    if (httpCode == 404) {
        Serial.printf("OTA: File not found (404): %s\n", _last_url);
        _ota_status = OTA_STATUS_FILE_NOT_FOUND;
        http.end();
        delete[] ca_cert;
        return false;
    }
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("OTA: HTTP error: %d\n", httpCode);
        _ota_status = OTA_STATUS_HTTP_ERROR;
        http.end();
        delete[] ca_cert;
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("OTA: Invalid content length");
        _ota_status = OTA_STATUS_HTTP_ERROR;
        http.end();
        delete[] ca_cert;
        return false;
    }

    Serial.printf("OTA: Image size: %d bytes\n", contentLength);

    // The filesystem partition cannot be rewritten while mounted; ota_loop() remounts it.
    if (update_type == U_SPIFFS) LittleFS.end();

    if (!Update.begin(contentLength, update_type)) {
        Serial.printf("OTA: Not enough space: %s\n", Update.errorString());
        _ota_status = OTA_STATUS_WRITE_ERROR;
        http.end();
        delete[] ca_cert;
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int written = 0;
    int last_progress = 0;
    int remaining = contentLength;
    uint32_t last_data_ms = millis();

    while (http.connected() && remaining > 0) {
        size_t available = stream->available();
        if (!available && millis() - last_data_ms >= OTA_STALL_TIMEOUT_MS) {
            Serial.printf("OTA: No data for %u s, giving up\n",
                          (unsigned)(OTA_STALL_TIMEOUT_MS / 1000));
            break;
        }
        if (available) {
            last_data_ms = millis();
            size_t to_read = (available < sizeof(buffer)) ? available : sizeof(buffer);
            size_t read_bytes = stream->readBytes(buffer, to_read);

            size_t written_bytes = Update.write(buffer, read_bytes);
            if (written_bytes != read_bytes) {
                Serial.printf("OTA: Write error: %s\n", Update.errorString());
                Update.abort();
                _ota_status = OTA_STATUS_WRITE_ERROR;
                http.end();
                delete[] ca_cert;
                return false;
            }

            written += written_bytes;
            remaining -= written_bytes;

            int progress = (written * 100) / contentLength;
            if (progress >= last_progress + 5) {
                last_progress = progress;
                Serial.printf("OTA: Progress %d%%\n", progress);
            }
        }
        delay(1);
    }

    if (remaining > 0) {
        Serial.printf("OTA: Download incomplete, %d of %d bytes received\n",
                      written, contentLength);
        Update.abort();
        _ota_status = OTA_STATUS_DOWNLOAD_INCOMPLETE;
        http.end();
        delete[] ca_cert;
        return false;
    }

    // Drain any TCP data buffered ahead by the stack so HTTPClient closes cleanly.
    uint8_t drain[64];
    while (stream->available()) {
        stream->readBytes(drain, min((size_t)stream->available(), sizeof(drain)));
    }

    if (!Update.end(true)) {
        Serial.printf("OTA: Update failed: %s\n", Update.errorString());
        _ota_status = OTA_STATUS_WRITE_ERROR;
        http.end();
        delete[] ca_cert;
        return false;
    }

    http.end();
    delete[] ca_cert;
    Serial.printf("OTA: %s update complete\n", update_type == U_SPIFFS ? "Filesystem" : "Firmware");
    return true;
}

void ota_loop() {
    // Staged-but-not-armed work waits here until ota_arm(), so both images can be queued
    // before either download starts.
    if (!_armed) return;
    if (!_ota_pending && !_fs_pending) { _armed = false; return; }

    bool ok = true;

    if (_fs_pending) {
        _fs_pending = false;
        ok = run_update(U_SPIFFS, _fs_url);
        // run_update() unmounts LittleFS to write the partition. Remount whatever is
        // there now (a no-op if it never got that far; formats if the write failed
        // partway) so the portal keeps serving when no reboot follows.
        LittleFS.begin(true);
    }
    if (ok && _ota_pending) {
        _ota_pending = false;
        ok = run_update(U_FLASH, _fw_url);
    }

    _armed = false;

    if (ok) {
        _ota_status = OTA_STATUS_COMPLETE;
        Serial.println("OTA: All updates complete, rebooting...");
        // Hold "complete" long enough for a client polling GET /api/ota once a second
        // (ebus-esp32 push --pull) to see it; the web server runs on its own task.
        delay(3000);
        ESP.restart();
    }
}
