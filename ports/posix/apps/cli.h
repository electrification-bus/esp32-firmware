#pragma once
// Command-line flags the two demo programs share.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BrokerArgs {
    const char* host = "localhost";
    int port = 1883;
    const char* user = nullptr;
    const char* password = nullptr;   // or EBUS_MQTT_PASSWORD, which stays out of `ps`
    const char* domain = "ebus";
    unsigned reconnect_ms = 2000;
    bool quiet = false;
};

static const char BROKER_USAGE[] =
    "  --host HOST          broker host (default localhost)\n"
    "  --port PORT          broker port (default 1883)\n"
    "  --user USER          broker username (default anonymous)\n"
    "  --password PASS      broker password (or set EBUS_MQTT_PASSWORD)\n"
    "  --domain DOMAIN      Homie topic domain (default ebus)\n"
    "  --reconnect-ms MS    reconnect interval (default 2000)\n"
    "  --quiet              no core or transport log on stderr\n";

// "--name VALUE" or "--name=VALUE". Advances *i past the value. Null when argv[*i] is not
// --name; exits when the value is missing.
inline const char* flag_value(int argc, char** argv, int* i, const char* name) {
    const char* a = argv[*i];
    size_t n = strlen(name);
    if (strncmp(a, name, n) != 0) return nullptr;
    if (a[n] == '=') return a + n + 1;
    if (a[n] != '\0') return nullptr;
    if (*i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", name);
        exit(2);
    }
    return argv[++*i];
}

// Consumes argv[*i] (and its value) if it is a broker flag; returns whether it was.
inline bool parse_broker_flag(int argc, char** argv, int* i, BrokerArgs* b) {
    const char* v;
    if ((v = flag_value(argc, argv, i, "--host"))) { b->host = v; return true; }
    if ((v = flag_value(argc, argv, i, "--port"))) { b->port = atoi(v); return true; }
    if ((v = flag_value(argc, argv, i, "--user"))) { b->user = v; return true; }
    if ((v = flag_value(argc, argv, i, "--password"))) { b->password = v; return true; }
    if ((v = flag_value(argc, argv, i, "--domain"))) { b->domain = v; return true; }
    if ((v = flag_value(argc, argv, i, "--reconnect-ms"))) {
        b->reconnect_ms = (unsigned)atoi(v);
        return true;
    }
    if (strcmp(argv[*i], "--quiet") == 0) { b->quiet = true; return true; }
    return false;
}

inline void broker_args_finish(BrokerArgs* b) {
    if (!b->password) b->password = getenv("EBUS_MQTT_PASSWORD");
    if (b->port <= 0 || b->port > 65535) {
        fprintf(stderr, "--port must be 1-65535\n");
        exit(2);
    }
}
