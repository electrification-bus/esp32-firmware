#pragma once

// mDNS service types and protocols
#define MDNS_PROTO_TCP          "_tcp"

// TXT keys and values follow "Detail: mDNS Discovery" in the eBus specification
// (electrification-bus/specification, framework.md).

// framework.md version this SDK implements, advertised as ebus_version
#define EBUS_SPEC_VERSION       "0.9"

// Keys shared by several services
#define MDNS_TXT_TXTVERS        "txtvers"
#define MDNS_VAL_TXTVERS        "1"
#define MDNS_TXT_DEVICE_ID      "device_id"
#define MDNS_TXT_DEVICE_TYPE    "device_type"
#define MDNS_TXT_MANUFACTURER   "manufacturer"
#define MDNS_TXT_MODEL          "model"
#define MDNS_TXT_FW_VERSION     "fw_version"

// _ebus._tcp - eBus entity discovery
#define MDNS_SVC_EBUS           "_ebus"
#define MDNS_TXT_EBUS_VERSION   "ebus_version"
#define MDNS_TXT_ROLES          "roles"
#define MDNS_TXT_NAME           "name"
#define MDNS_TXT_AUTH_METHODS   "auth_methods"
#define MDNS_VAL_ROLE_DEVICE    "device"
#define MDNS_VAL_ROLE_CONTROLLER "controller"
#define MDNS_TXT_REGISTER       "register"
#define MDNS_VAL_REGISTER       "/api/v1/auth/register"
// passphrase: POST MDNS_VAL_REGISTER exchanges the device passphrase for a token.
// preconfigured: the passphrase itself is set out of band (config file, setup page).
#define MDNS_VAL_AUTH_METHODS   "passphrase,preconfigured"
// Not in framework.md 0.9: the keys shipping eBus devices (SPAN panels) advertise
#define MDNS_TXT_HOMIE_DOMAIN   "homie_domain"
#define MDNS_TXT_HOMIE_VERSION  "homie_version"
#define MDNS_TXT_HOMIE_ROLES    "homie_roles"

// _mqtt._tcp - MQTT broker advertisement (RFC 6763)
#define MDNS_SVC_MQTT           "_mqtt"
#define MDNS_TXT_MQTT_HOST      "host"
#define MDNS_TXT_MQTT_PORT      "port"
#define MDNS_TXT_MQTT_TLS       "tls"
#define MDNS_TXT_MQTT_AUTH      "auth"

// _device-info._tcp - Hardware/firmware information
#define MDNS_SVC_DEVICE_INFO    "_device-info"
#define MDNS_TXT_SERIAL_NUMBER  "serial_number"
#define MDNS_TXT_OS_VERSION     "os_version"
#define MDNS_TXT_MAC            "mac"

// _http._tcp - HTTP REST API with OpenAPI spec
#define MDNS_SVC_HTTP           "_http"
#define MDNS_TXT_HTTP_PATH      "path"
#define MDNS_TXT_HTTP_VERSION   "version"
#define MDNS_TXT_HTTP_OPENAPI   "openapi"
#define MDNS_VAL_HTTP_PATH      "/api/v1"
#define MDNS_VAL_HTTP_VERSION   "1.3.0"           // keep equal to info.version in data/openapi.yml
#define MDNS_VAL_HTTP_OPENAPI   "/api/v1/openapi.yml"

// _telnet._tcp - serial-over-TCP console log stream (include/platform/log_stream.h).
// Not in framework.md; proposed upstream as electrification-bus/specification issue 25.
#define MDNS_SVC_LOG            "_telnet"
#define MDNS_TXT_LOG_KIND       "kind"
#define MDNS_VAL_LOG_KIND       "serial-log"    // read-only log tap, not a shell

// Common TXT record values
#define MDNS_VAL_TRUE           "true"
#define MDNS_VAL_FALSE          "false"

// mDNS discovery
#define MDNS_QUERY_SECURE_MQTT  "secure-mqtt"   // _secure-mqtt._tcp (TLS, 8883) — eBus primary
#define MDNS_QUERY_MQTT         "mqtt"          // _mqtt._tcp (plain, 1883) — only when TLS infeasible
#define MDNS_QUERY_TCP          "tcp"
