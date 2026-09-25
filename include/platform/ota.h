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

// Queue a firmware (app partition) update from the configured firmware_url.
// Has no effect if OTA is not enabled or no URL is configured.
//
// start_now=false STAGES the pull instead of beginning it, so a filesystem and a
// firmware update can both be queued and then run as one session with a single reboot
// (ota_loop() always runs the filesystem first). Without it the first pull starts
// immediately and the second request races the download it is already performing.
// This is the pull-side counterpart of the push path's ?reboot=0.
void ota_start_update(bool start_now = true);

// Queue a filesystem (LittleFS) update from the configured fs_url. See above for
// start_now.
void ota_start_fs_update(bool start_now = true);

// Begin whatever has been staged. Safe to call with nothing staged.
void ota_arm();

// Process pending OTA update - call from main loop.
void ota_loop();

// Status accessors for captive portal reporting.
OtaStatus    ota_get_status();
const char*  ota_get_status_str();
const char*  ota_get_last_host();
const char*  ota_get_last_file();
const char*  ota_get_last_url();
int          ota_get_last_http_code();
