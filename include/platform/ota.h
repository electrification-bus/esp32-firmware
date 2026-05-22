#pragma once
#include <Arduino.h>

typedef enum {
    OTA_STATUS_IDLE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_HOST_NOT_FOUND,
    OTA_STATUS_FILE_NOT_FOUND,
    OTA_STATUS_HTTP_ERROR,
    OTA_STATUS_WRITE_ERROR,
    OTA_STATUS_COMPLETE
} OtaStatus;

// Queue a firmware update from the configured firmware_url.
// Has no effect if OTA is not enabled or no URL is configured.
void ota_start_update();

// Process pending OTA update - call from main loop.
void ota_loop();

// Status accessors for captive portal reporting.
OtaStatus    ota_get_status();
const char*  ota_get_status_str();
const char*  ota_get_last_host();
const char*  ota_get_last_file();
const char*  ota_get_last_url();
int          ota_get_last_http_code();
