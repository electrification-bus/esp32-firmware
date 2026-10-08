#pragma once
// Binds the core's log sink to a recorder for host tests: it prints nothing, counts the
// lines that carry "**ERROR" and keeps the last line and the last error line.
#include <homie/homie_log.h>
#include <stdio.h>
#include <string.h>

struct LogCapture {
    int lines = 0;
    int errors = 0;
    char last[256] = {0};
    char last_error[256] = {0};

    void clear() { lines = 0; errors = 0; last[0] = '\0'; last_error[0] = '\0'; }
};

inline LogCapture g_log;

inline void log_capture_vprintf(const char* fmt, va_list args) {
    char line[256];
    vsnprintf(line, sizeof(line), fmt, args);
    g_log.lines++;
    snprintf(g_log.last, sizeof(g_log.last), "%s", line);
    if (strstr(line, "**ERROR")) {
        g_log.errors++;
        snprintf(g_log.last_error, sizeof(g_log.last_error), "%s", line);
    }
}

inline void log_capture_bind() { homie_log_bind(log_capture_vprintf); }
