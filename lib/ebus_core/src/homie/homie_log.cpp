#include <homie/homie_log.h>
#include <stdio.h>

static void stdout_vprintf(const char* fmt, va_list args) { vprintf(fmt, args); }

static homie_log_vprintf_t _sink = stdout_vprintf;

void homie_log_bind(homie_log_vprintf_t sink) { _sink = sink ? sink : stdout_vprintf; }

void homie_logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    _sink(fmt, args);
    va_end(args);
}

void homie_logln(const char* s) { homie_logf("%s\r\n", s); }
