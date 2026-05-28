#include <platform/ota.h>
#include <platform/config.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

static bool      _ota_pending      = false;
static bool      _fs_pending       = false;
static OtaStatus _ota_status       = OTA_STATUS_IDLE;
static char      _last_url[256]    = {0};
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
        case OTA_STATUS_COMPLETE:       return "complete";
        default:                        return "unknown";
    }
}

void ota_start_update() {
    if (!ota_is_enabled()) {
        Serial.println("OTA: Update rejected - OTA not enabled");
        return;
    }
    if (firmware_url[0] == '\0') {
        Serial.println("OTA: Update rejected - no firmware URL");
        return;
    }
    strncpy(_last_url, firmware_url, sizeof(_last_url) - 1);
    _last_url[sizeof(_last_url) - 1] = '\0';
    parse_url(_last_url);
    _last_http_code = 0;
    _ota_pending = true;
    Serial.printf("OTA: Update queued from %s\n", _last_url);
}

void ota_start_fs_update() {
    if (!ota_is_enabled()) {
        Serial.println("OTA: FS update rejected - OTA not enabled");
        return;
    }
    if (fs_url[0] == '\0') {
        Serial.println("OTA: FS update rejected - no fs URL");
        return;
    }
    strncpy(_last_url, fs_url, sizeof(_last_url) - 1);
    _last_url[sizeof(_last_url) - 1] = '\0';
    parse_url(_last_url);
    _last_http_code = 0;
    _fs_pending = true;
    Serial.printf("OTA: FS update queued from %s\n", _last_url);
}

static bool run_update(int update_type) {
    HTTPClient http;
    WiFiClient client;

    Serial.printf("OTA: Starting %s update from %s\n",
                  update_type == U_SPIFFS ? "filesystem" : "firmware", _last_url);
    _ota_status = OTA_STATUS_DOWNLOADING;

    if (!http.begin(client, _last_url)) {
        Serial.println("OTA: Failed to connect to server");
        _ota_status = OTA_STATUS_HOST_NOT_FOUND;
        return false;
    }

    int httpCode = http.GET();
    _last_http_code = httpCode;

    if (httpCode == 404) {
        Serial.printf("OTA: File not found (404): %s\n", _last_url);
        _ota_status = OTA_STATUS_FILE_NOT_FOUND;
        http.end();
        return false;
    }
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("OTA: HTTP error: %d\n", httpCode);
        _ota_status = OTA_STATUS_HTTP_ERROR;
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("OTA: Invalid content length");
        _ota_status = OTA_STATUS_HTTP_ERROR;
        http.end();
        return false;
    }

    Serial.printf("OTA: Image size: %d bytes\n", contentLength);

    if (!Update.begin(contentLength, update_type)) {
        Serial.printf("OTA: Not enough space: %s\n", Update.errorString());
        _ota_status = OTA_STATUS_WRITE_ERROR;
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int written = 0;
    int last_progress = 0;
    int remaining = contentLength;

    while (http.connected() && remaining > 0) {
        size_t available = stream->available();
        if (available) {
            size_t to_read = (available < sizeof(buffer)) ? available : sizeof(buffer);
            size_t read_bytes = stream->readBytes(buffer, to_read);

            size_t written_bytes = Update.write(buffer, read_bytes);
            if (written_bytes != read_bytes) {
                Serial.printf("OTA: Write error: %s\n", Update.errorString());
                Update.abort();
                _ota_status = OTA_STATUS_WRITE_ERROR;
                http.end();
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

    // Drain any TCP data buffered ahead by the stack so HTTPClient closes cleanly.
    uint8_t drain[64];
    while (stream->available()) {
        stream->readBytes(drain, min((size_t)stream->available(), sizeof(drain)));
    }

    if (!Update.end(true)) {
        Serial.printf("OTA: Update failed: %s\n", Update.errorString());
        _ota_status = OTA_STATUS_WRITE_ERROR;
        http.end();
        return false;
    }

    http.end();
    Serial.printf("OTA: %s update complete\n", update_type == U_SPIFFS ? "Filesystem" : "Firmware");
    return true;
}

void ota_loop() {
    if (!_ota_pending && !_fs_pending) return;

    bool ok = true;

    if (_fs_pending) {
        _fs_pending = false;
        ok = run_update(U_SPIFFS);
    }
    if (ok && _ota_pending) {
        _ota_pending = false;
        ok = run_update(U_FLASH);
    }

    if (ok) {
        _ota_status = OTA_STATUS_COMPLETE;
        Serial.println("OTA: All updates complete, rebooting...");
        delay(500);
        ESP.restart();
    }
}
