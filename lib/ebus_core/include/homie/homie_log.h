#pragma once
// Console output for the core. Every diagnostic the core prints goes through
// homie_logf() to one printf-style sink. Unbound, the sink is vprintf() to stdout; the
// ESP32 port binds it to the UART + TCP console tee (src/platform/homie_port.cpp).
#include <stdarg.h>

typedef void (*homie_log_vprintf_t)(const char* fmt, va_list args);

// Bind the sink; null restores the stdout default. Bind before anything logs from another
// task: the pointer is read without a lock.
void homie_log_bind(homie_log_vprintf_t sink);

void homie_logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// `s` and a CR LF, the line ending Arduino's println() writes.
void homie_logln(const char* s);
