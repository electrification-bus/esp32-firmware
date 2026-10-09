// Host-side tests for src/platform/mdns_services.cpp: the services and TXT records
// setup_mdns() advertises, recorded through a fake MdnsBackend. The expected lists are
// what network.cpp's mdns_register_services() passed to MDNS.addService() and
// MDNS.addServiceTxt() before it used the core's builders (network.cpp at faaf557, lines
// cited per service), so a change in any key, value, order or port fails here.

#define USE_EBUS_TOPIC   // as platformio.ini's [env] build_flags set it for the firmware
#include <unity.h>
#include "../../../src/platform/mdns_services.cpp"

#include <stdio.h>

class RecordingBackend : public MdnsBackend {
 public:
  char lines[64][300];
  int count = 0;
  bool refuse = false;

  bool advertise(const char* service_type, uint16_t port, const TxtPair* txt,
                 uint8_t txt_count) override {
    if (refuse) return false;
    snprintf(lines[count++], sizeof(lines[0]), "SRV %s %u", service_type, (unsigned)port);
    for (uint8_t i = 0; i < txt_count; i++) {
      snprintf(lines[count++], sizeof(lines[0]), "TXT %s %s=%s", service_type, txt[i].key,
               txt[i].value);
    }
    return true;
  }
  int browse(const char*, uint32_t, mdns_instance_fn, void*) override { return 0; }
  bool resolve(const char*, uint32_t, char*, size_t) override { return false; }
};

static void expect_lines(const RecordingBackend& b, const char* const* expected, int n) {
  for (int i = 0; i < n && i < b.count; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], b.lines[i]);
  }
  TEST_ASSERT_EQUAL_INT(n, b.count);
}

// The example device.yml with every optional value set and the log stream listening.
static MdnsServicesConfig full_config(void) {
  MdnsServicesConfig cfg = {};
  cfg.device_id = "b0b21c90f570";
  cfg.homie_role = "device";
  cfg.manufacturer = "NESL";
  cfg.model = "ESP32-POE-ISO";
  cfg.serial = "SN-42";
  cfg.device_type = "energy.ebus.device.esp32";
  cfg.name = "Garage Sensor";
  cfg.fw_version = "1.0.0";
  cfg.os_version = "arduino-esp32 3.3.11 / ESP-IDF 5.5.5";
  cfg.mac = "b0b21c90f573";
  cfg.http_port = 80;
  cfg.ebus_port = 8883;
  cfg.log_port = 23;
  return cfg;
}

static void test_full_config(void) {
  static const char* const expected[] = {
      // network.cpp:374-381
      "SRV _device-info._tcp 80",
      "TXT _device-info._tcp txtvers=1",
      "TXT _device-info._tcp manufacturer=NESL",
      "TXT _device-info._tcp model=ESP32-POE-ISO",
      "TXT _device-info._tcp serial_number=SN-42",
      "TXT _device-info._tcp fw_version=1.0.0",
      "TXT _device-info._tcp os_version=arduino-esp32 3.3.11 / ESP-IDF 5.5.5",
      "TXT _device-info._tcp mac=b0b21c90f573",
      // network.cpp:387-393
      "SRV _http._tcp 80",
      "TXT _http._tcp txtvers=1",
      "TXT _http._tcp path=/api/v1",
      "TXT _http._tcp version=1.3.0",
      "TXT _http._tcp device_id=b0b21c90f570",
      "TXT _http._tcp device_type=energy.ebus.device.esp32",
      "TXT _http._tcp openapi=/api/v1/openapi.yml",
      // network.cpp:400-404
      "SRV _telnet._tcp 23",
      "TXT _telnet._tcp txtvers=1",
      "TXT _telnet._tcp device_id=b0b21c90f570",
      "TXT _telnet._tcp kind=serial-log",
      // network.cpp:411-427
      "SRV _ebus._tcp 8883",
      "TXT _ebus._tcp txtvers=1",
      "TXT _ebus._tcp ebus_version=0.9",
      "TXT _ebus._tcp roles=device",
      "TXT _ebus._tcp device_id=b0b21c90f570",
      "TXT _ebus._tcp device_type=energy.ebus.device.esp32",
      "TXT _ebus._tcp name=Garage Sensor",
      "TXT _ebus._tcp manufacturer=NESL",
      "TXT _ebus._tcp model=ESP32-POE-ISO",
      "TXT _ebus._tcp fw_version=1.0.0",
      "TXT _ebus._tcp register=/api/v1/auth/register",
      "TXT _ebus._tcp auth_methods=passphrase,preconfigured",
      "TXT _ebus._tcp homie_domain=ebus",
      "TXT _ebus._tcp homie_version=5",
      "TXT _ebus._tcp homie_roles=device",
  };
  RecordingBackend b;
  MdnsServicesConfig cfg = full_config();
  MdnsServicesResult r;
  mdns_services_advertise(&b, &cfg, &r);
  expect_lines(b, expected, (int)(sizeof(expected) / sizeof(expected[0])));
  for (int i = 0; i < MDNS_SERVICE_COUNT; i++) {
    TEST_ASSERT_EQUAL_UINT8(MDNS_SERVICE_ADVERTISED, r.status[i]);
  }
}

// network.cpp:359-362 omits an empty recommended value (device_type, name, fw_version);
// 369 falls back to the device id for serial_number; 400 skips _telnet._tcp while the log
// stream is down; 411-412 advertise "controller" in roles and homie_roles.
static void test_controller_with_defaults(void) {
  static const char* const expected[] = {
      "SRV _device-info._tcp 80",
      "TXT _device-info._tcp txtvers=1",
      "TXT _device-info._tcp manufacturer=unknown",
      "TXT _device-info._tcp model=ESP32",
      "TXT _device-info._tcp serial_number=b0b21c90f570",
      "TXT _device-info._tcp os_version=arduino-esp32 3.3.11 / ESP-IDF 5.5.5",
      "TXT _device-info._tcp mac=b0b21c90f573",
      "SRV _http._tcp 80",
      "TXT _http._tcp txtvers=1",
      "TXT _http._tcp path=/api/v1",
      "TXT _http._tcp version=1.3.0",
      "TXT _http._tcp device_id=b0b21c90f570",
      "TXT _http._tcp openapi=/api/v1/openapi.yml",
      "SRV _ebus._tcp 1883",
      "TXT _ebus._tcp txtvers=1",
      "TXT _ebus._tcp ebus_version=0.9",
      "TXT _ebus._tcp roles=controller",
      "TXT _ebus._tcp device_id=b0b21c90f570",
      "TXT _ebus._tcp manufacturer=unknown",
      "TXT _ebus._tcp model=ESP32",
      "TXT _ebus._tcp register=/api/v1/auth/register",
      "TXT _ebus._tcp auth_methods=passphrase,preconfigured",
      "TXT _ebus._tcp homie_domain=ebus",
      "TXT _ebus._tcp homie_version=5",
      "TXT _ebus._tcp homie_roles=controller",
  };
  RecordingBackend b;
  MdnsServicesConfig cfg = full_config();
  cfg.homie_role = "controller";
  cfg.manufacturer = "unknown";
  cfg.model = "ESP32";
  cfg.serial = "";
  cfg.device_type = "";
  cfg.name = "";
  cfg.fw_version = "";
  cfg.ebus_port = 1883;
  cfg.log_port = 0;
  MdnsServicesResult r;
  mdns_services_advertise(&b, &cfg, &r);
  expect_lines(b, expected, (int)(sizeof(expected) / sizeof(expected[0])));
  TEST_ASSERT_EQUAL_UINT8(MDNS_SERVICE_SKIPPED, r.status[MDNS_SERVICE_LOG]);
}

// network.cpp:411-412: any homie_role other than "controller" advertises device.
static void test_unknown_role_is_device(void) {
  MdnsServicesConfig cfg = full_config();
  cfg.homie_role = "bridge";
  EbusIdentity id;
  mdns_services_identity(&cfg, &id);
  TEST_ASSERT_EQUAL_UINT8(EBUS_ROLE_DEVICE, id.roles);
}

static void test_backend_refusal_is_reported(void) {
  RecordingBackend b;
  b.refuse = true;
  MdnsServicesConfig cfg = full_config();
  MdnsServicesResult r;
  mdns_services_advertise(&b, &cfg, &r);
  for (int i = 0; i < MDNS_SERVICE_COUNT; i++) {
    TEST_ASSERT_EQUAL_UINT8(MDNS_SERVICE_REFUSED, r.status[i]);
    TEST_ASSERT_EQUAL_UINT8(TXT_OK, r.txt[i]);
  }
}

static void test_build_failure_is_reported(void) {
  RecordingBackend b;
  MdnsServicesConfig cfg = full_config();
  cfg.device_id = "";
  MdnsServicesResult r;
  mdns_services_advertise(&b, &cfg, &r);
  TEST_ASSERT_EQUAL_UINT8(MDNS_SERVICE_BUILD_FAILED, r.status[MDNS_SERVICE_HTTP]);
  TEST_ASSERT_EQUAL_UINT8(TXT_MISSING, r.txt[MDNS_SERVICE_HTTP]);
  TEST_ASSERT_EQUAL_UINT8(MDNS_SERVICE_BUILD_FAILED, r.status[MDNS_SERVICE_EBUS]);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_full_config);
  RUN_TEST(test_controller_with_defaults);
  RUN_TEST(test_unknown_role_is_device);
  RUN_TEST(test_backend_refusal_is_reported);
  RUN_TEST(test_build_failure_is_reported);
  return UNITY_END();
}
