#include <etc/portal_prefs.h>
#include <Preferences.h>
#include <etc/config.h>

#define PREFS_NAMESPACE "portal"
#define PREFS_KEY_WIFI_SSID "wifi_ssid"
#define PREFS_KEY_WIFI_PASS "wifi_pass"
#define PREFS_KEY_MQTT_HOST "mqtt_host"
#define PREFS_KEY_MQTT_PORT "mqtt_port"
#define PREFS_KEY_MQTT_USER "mqtt_user"
#define PREFS_KEY_MQTT_PASS "mqtt_pass"

static Preferences prefs;

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

void portal_prefs_load_fallbacks() {
    prefs.begin(PREFS_NAMESPACE, true);  // read-only

    // Portal-saved values override config.yml — the user explicitly configured
    // these at runtime, so they should take priority on subsequent boots.
    // Config.yml serves as the initial default; portal settings are the override.

    char buf[128];

    size_t len = prefs.getString(PREFS_KEY_WIFI_SSID, buf, sizeof(buf));
    if (len > 0) {
        strncpy(wifi_ssid, buf, sizeof(wifi_ssid) - 1);
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
        Serial.printf("PREFS: Loaded WiFi SSID='%s'\n", wifi_ssid);
    }

    len = prefs.getString(PREFS_KEY_WIFI_PASS, buf, sizeof(buf));
    if (len > 0) {
        strncpy(wifi_password, buf, sizeof(wifi_password) - 1);
        wifi_password[sizeof(wifi_password) - 1] = '\0';
    }

    len = prefs.getString(PREFS_KEY_MQTT_HOST, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_host, buf, sizeof(mqtt_host) - 1);
        mqtt_host[sizeof(mqtt_host) - 1] = '\0';
        Serial.printf("PREFS: Loaded MQTT broker='%s'\n", mqtt_host);
    }

    uint16_t saved_port = prefs.getUShort(PREFS_KEY_MQTT_PORT, 0);
    if (saved_port > 0) {
        mqtt_port = saved_port;
        Serial.printf("PREFS: Loaded MQTT port=%u\n", mqtt_port);
    }

    len = prefs.getString(PREFS_KEY_MQTT_USER, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_username, buf, sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
    }

    len = prefs.getString(PREFS_KEY_MQTT_PASS, buf, sizeof(buf));
    if (len > 0) {
        strncpy(mqtt_password, buf, sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
    }

    prefs.end();
}

void portal_prefs_clear() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("PREFS: Cleared all portal settings (WiFi + MQTT)");
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
