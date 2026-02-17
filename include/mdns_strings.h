#pragma once

// mDNS service types and protocols
#define MDNS_PROTO_TCP          "_tcp"

// _homie._tcp - Homie protocol device/controller discovery
#define MDNS_SVC_HOMIE          "_homie"
#define MDNS_TXT_HOMIE_VERSION  "homie"
#define MDNS_TXT_HOMIE_DOC_VER  "version"
#define MDNS_TXT_HOMIE_ID       "id"
#define MDNS_TXT_HOMIE_NAME     "name"
#define MDNS_TXT_HOMIE_ROLE     "role"
#define MDNS_TXT_HOMIE_TYPE     "type"
#define MDNS_TXT_HOMIE_BASE     "base"
#define MDNS_TXT_HOMIE_STATE    "state"
#define MDNS_TXT_HOMIE_IMPL     "implementation"
#define MDNS_VAL_HOMIE_VERSION  "5.0"
#define MDNS_VAL_HOMIE_DOC_VER  "1"
#define MDNS_VAL_HOMIE_IMPL     "homie-esp32-sdk"

// _mqtt._tcp - MQTT broker advertisement (RFC 6763)
#define MDNS_SVC_MQTT           "_mqtt"
#define MDNS_TXT_MQTT_HOST      "host"
#define MDNS_TXT_MQTT_PORT      "port"
#define MDNS_TXT_MQTT_TLS       "tls"
#define MDNS_TXT_MQTT_AUTH      "auth"

// _device-info._tcp - Hardware/firmware information
#define MDNS_SVC_DEVICE_INFO    "_device-info"
#define MDNS_TXT_MANUFACTURER   "manufacturer"
#define MDNS_TXT_MODEL          "model"
#define MDNS_TXT_SERIAL_NUMBER  "serial_number"
#define MDNS_TXT_ETH_MAC        "eth_mac"
#define MDNS_TXT_WLAN_MAC       "wlan_mac"
#define MDNS_TXT_FW_NAME        "fw_name"
#define MDNS_TXT_FW_VERSION     "fw_version"
#define MDNS_TXT_PLATFORM       "platform"
#define MDNS_TXT_SDK_VERSION    "sdk_version"
#define MDNS_TXT_ETH_IP         "eth_ip"
#define MDNS_TXT_WLAN_IP        "wlan_ip"
#define MDNS_VAL_PLATFORM       "ESP32"

// Common TXT record values
#define MDNS_VAL_TRUE           "true"
#define MDNS_VAL_FALSE          "false"

// mDNS discovery
#define MDNS_QUERY_MQTT         "mqtt"
#define MDNS_QUERY_TCP          "tcp"
