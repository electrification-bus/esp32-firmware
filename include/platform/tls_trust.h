#pragma once

#include <stdint.h>
#include <WiFiClientSecure.h>

// The public root CAs: ESP-IDF's certificate bundle (Mozilla's root list), which the
// precompiled arduino-esp32 3.x framework carries in libmbedtls. A host whose
// /certs/<Host>.ca is missing is verified against it. On arduino-esp32 2.x both return false.

// Make `client` verify servers against the public roots.
bool tls_trust_public_roots(WiFiClientSecure& client);

// Connect to host:port trusting the public roots and disconnect. True when the server's
// certificate verified; `client` is then left trusting the public roots.
bool tls_public_roots_verify(WiFiClientSecure& client, const char* host, uint16_t port);
