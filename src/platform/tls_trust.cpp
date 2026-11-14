#include <platform/tls_trust.h>
#include <platform/network.h>

#include <esp_arduino_version.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3

// The bundle object in the framework's libmbedtls.a (x509_crt_bundle.S).
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

bool tls_trust_public_roots(WiFiClientSecure& client) {
    client.setCACertBundle(x509_crt_bundle_start,
                           static_cast<size_t>(x509_crt_bundle_end - x509_crt_bundle_start));
    return true;
}

bool tls_public_roots_verify(WiFiClientSecure& client, const char* host, uint16_t port) {
    tls_trust_public_roots(client);
    network_dns_prime(host);   // connect() resolves host; see network.cpp
    bool ok = client.connect(host, port);
    client.stop();
    return ok;
}

#else

bool tls_trust_public_roots(WiFiClientSecure&) { return false; }
bool tls_public_roots_verify(WiFiClientSecure&, const char*, uint16_t) { return false; }

#endif
