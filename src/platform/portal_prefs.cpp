#include <platform/portal_prefs.h>
#include <Preferences.h>
#include <platform/config.h>

#define PREFS_NAMESPACE "portal"
#define PREFS_KEY_WIFI_SSID "wifi_ssid"
#define PREFS_KEY_WIFI_PASS "wifi_pass"
#define PREFS_KEY_MQTT_HOST "mqtt_host"
#define PREFS_KEY_MQTT_PORT "mqtt_port"
#define PREFS_KEY_MQTT_USER   "mqtt_user"
#define PREFS_KEY_MQTT_PASS   "mqtt_pass"
#define PREFS_KEY_PASSPHRASE  "passphrase"
#define PREFS_KEY_FW_URL      "fw_url"
#define PREFS_KEY_FS_URL      "fs_url"

static Preferences prefs;

// True once the corresponding OTA URL came from the portal (NVS at boot, or a
// portal save this session) — distinguishes user-entered URLs from config.yml
// defaults so the UI only prefills what the user actually configured.
static bool _fw_url_from_portal = false;
static bool _fs_url_from_portal = false;

bool portal_prefs_has_fw_url() { return _fw_url_from_portal; }
bool portal_prefs_has_fs_url() { return _fs_url_from_portal; }

void portal_prefs_save_wifi(const char* ssid, const char* password) {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_WIFI_SSID, ssid);
    prefs.putString(PREFS_KEY_WIFI_PASS, password);
    prefs.end();
    Serial.printf("PREFS: Saved WiFi SSID='%s'\n", ssid);
}

void portal_prefs_save_mqtt(const char* host, uint16_t port, const char* user, const char* pass) {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_MQTT_HOST, host);
    prefs.putUShort(PREFS_KEY_MQTT_PORT, port);
    prefs.putString(PREFS_KEY_MQTT_USER, user);
    prefs.putString(PREFS_KEY_MQTT_PASS, pass);
    prefs.end();
    Serial.printf("PREFS: Saved MQTT broker='%s' port=%u\n", host, port);
}

void portal_prefs_save_passphrase(const char* passphrase) {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_PASSPHRASE, passphrase);
    prefs.end();
    Serial.println("PREFS: Saved device passphrase");
}

void portal_prefs_save_firmware_url(const char* url) {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_FW_URL, url);
    prefs.end();
    _fw_url_from_portal = true;
    Serial.printf("PREFS: Saved firmware URL='%s'\n", url);
}

void portal_prefs_save_fs_url(const char* url) {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_KEY_FS_URL, url);
    prefs.end();
    _fs_url_from_portal = true;
    Serial.printf("PREFS: Saved filesystem URL='%s'\n", url);
}

void portal_prefs_load_fallbacks() {
    prefs.begin(PREFS_NAMESPACE, true);  // read-only

    char buf[128];
    bool any = false;

    size_t len = prefs.getString(PREFS_KEY_WIFI_SSID, buf, sizeof(buf));
    if (len > 0) {
        strncpy(wifi_ssid, buf, sizeof(wifi_ssid) - 1);
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
        Serial.printf("PREFS: NVRAM wifi_ssid='%s'\n", wifi_ssid);
        any = true;
    }

    len = prefs.getString(PREFS_KEY_WIFI_PASS, buf, sizeof(buf));
    if (len > 0) {
        strncpy(wifi_password, buf, sizeof(wifi_password) - 1);
        wifi_password[sizeof(wifi_password) - 1] = '\0';
        Serial.println("PREFS: NVRAM wifi_password=<set>");
        any = true;
    }

    bool mqtt_any = false;

    len = prefs.getString(PREFS_KEY_MQTT_HOST, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_host, buf, sizeof(mqtt_host) - 1);
        mqtt_host[sizeof(mqtt_host) - 1] = '\0';
        Serial.printf("PREFS: NVRAM mqtt_host='%s'\n", mqtt_host);
        any = mqtt_any = true;
    }

    uint16_t saved_port = prefs.getUShort(PREFS_KEY_MQTT_PORT, 0);
    if (saved_port > 0) {
        mqtt_port = saved_port;
        Serial.printf("PREFS: NVRAM mqtt_port=%u\n", mqtt_port);
        any = mqtt_any = true;
    }

    len = prefs.getString(PREFS_KEY_MQTT_USER, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_username, buf, sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
        Serial.printf("PREFS: NVRAM mqtt_username='%s'\n", mqtt_username);
        any = mqtt_any = true;
    }

    len = prefs.getString(PREFS_KEY_MQTT_PASS, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_password, buf, sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
        Serial.println("PREFS: NVRAM mqtt_password=<set>");
        any = mqtt_any = true;
    }

    if (mqtt_any)
        Serial.println("PREFS: NVRAM PREF OVERRIDES CONFIG: clear via POST /api/reset-prefs");

    len = prefs.getString(PREFS_KEY_FW_URL, buf, sizeof(buf));
    if (len > 0) {
        strncpy(firmware_url, buf, sizeof(firmware_url) - 1);
        firmware_url[sizeof(firmware_url) - 1] = '\0';
        _fw_url_from_portal = true;
        Serial.printf("PREFS: NVRAM firmware_url='%s'\n", firmware_url);
        any = true;
    }

    len = prefs.getString(PREFS_KEY_FS_URL, buf, sizeof(buf));
    if (len > 0) {
        strncpy(fs_url, buf, sizeof(fs_url) - 1);
        fs_url[sizeof(fs_url) - 1] = '\0';
        _fs_url_from_portal = true;
        Serial.printf("PREFS: NVRAM fs_url='%s'\n", fs_url);
        any = true;
    }

    len = prefs.getString(PREFS_KEY_PASSPHRASE, buf, sizeof(buf));
    if (len > 0) {
        strncpy(device_passphrase, buf, sizeof(device_passphrase) - 1);
        device_passphrase[sizeof(device_passphrase) - 1] = '\0';
        Serial.println("PREFS: NVRAM device_passphrase=<set>");
        Serial.println("PREFS: NVRAM PREF OVERRIDES CONFIG: clear via POST /api/reset-prefs");
        any = true;
    }

    if (!any)
        Serial.println("PREFS: NVRAM empty — using config.yml values only");

    prefs.end();
}

void portal_prefs_clear() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("PREFS: NVRAM reset via captive portal — all settings cleared, reboot to use config.yml");
}

// --- Auth rate limiting (RAM only, resets on reboot) ---
static uint8_t _auth_failures = 0;
static unsigned long _auth_lockout_start = 0;

bool portal_auth_is_locked() {
    if (_auth_failures < AUTH_MAX_FAILURES) return false;
    if (millis() - _auth_lockout_start >= AUTH_LOCKOUT_MS) {
        _auth_failures = 0;
        Serial.println("AUTH: Lockout expired");
        return false;
    }
    return true;
}

void portal_auth_record_failure() {
    _auth_failures++;
    Serial.printf("AUTH: Failure %d/%d\n", _auth_failures, AUTH_MAX_FAILURES);
    if (_auth_failures >= AUTH_MAX_FAILURES) {
        _auth_lockout_start = millis();
        Serial.println("AUTH: Locked out for 5 minutes");
    }
}

void portal_auth_reset() {
    _auth_failures = 0;
}

// --- Portal session tokens (RAM only, single slot — one admin at a time) ---
#define SESSION_TOKEN_LEN 32
static char _session_token[SESSION_TOKEN_LEN + 1] = {0};
static unsigned long _session_created = 0;
static unsigned long _session_last_use = 0;

const char* portal_session_create() {
    // esp_random() draws from the hardware TRNG (WiFi/BT RF noise) — 128 bits
    for (int i = 0; i < SESSION_TOKEN_LEN; i += 8) {
        snprintf(&_session_token[i], 9, "%08lx", (unsigned long)esp_random());
    }
    _session_created = _session_last_use = millis();
    Serial.println("SESSION: token issued (15 min idle / 2 h max)");
    return _session_token;
}

void portal_session_clear() {
    _session_token[0] = '\0';
}

bool portal_session_valid(const char* token) {
    if (_session_token[0] == '\0' || token == NULL) return false;
    unsigned long now = millis();
    if (now - _session_last_use > SESSION_IDLE_TIMEOUT_MS ||
        now - _session_created > SESSION_MAX_LIFETIME_MS) {
        portal_session_clear();
        Serial.println("SESSION: expired");
        return false;
    }
    if (strlen(token) != SESSION_TOKEN_LEN) return false;  // length isn't secret
    // Constant-time compare — no early exit, so timing doesn't leak a prefix match
    uint8_t diff = 0;
    for (int i = 0; i < SESSION_TOKEN_LEN; i++) {
        diff |= (uint8_t)token[i] ^ (uint8_t)_session_token[i];
    }
    if (diff != 0) return false;
    _session_last_use = now;  // sliding idle window
    return true;
}
