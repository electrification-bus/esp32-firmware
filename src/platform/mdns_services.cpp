#include <platform/mdns_services.h>
#include <ebus/discovery/mdns_strings.h>
#include <homie/homie.h>
#include <string.h>

const char* mdns_service_type(uint8_t service) {
  switch (service) {
    case MDNS_SERVICE_DEVICE_INFO: return MDNS_TYPE_DEVICE_INFO;
    case MDNS_SERVICE_HTTP:        return MDNS_TYPE_HTTP;
    case MDNS_SERVICE_LOG:         return MDNS_TYPE_LOG;
    case MDNS_SERVICE_EBUS:        return MDNS_TYPE_EBUS;
    default:                       return "";
  }
}

void mdns_services_identity(const MdnsServicesConfig* cfg, EbusIdentity* id) {
  memset(id, 0, sizeof(*id));
  id->device_id = cfg->device_id;
  id->roles = (cfg->homie_role != nullptr && strcmp(cfg->homie_role, MDNS_VAL_ROLE_CONTROLLER) == 0)
                  ? EBUS_ROLE_CONTROLLER : EBUS_ROLE_DEVICE;
  id->manufacturer = cfg->manufacturer;
  id->model = cfg->model;
  id->serial_number = (cfg->serial != nullptr && cfg->serial[0] != '\0') ? cfg->serial
                                                                         : cfg->device_id;
  id->device_type = cfg->device_type;
  id->name = cfg->name;
  id->fw_version = cfg->fw_version;
  id->register_path = MDNS_VAL_REGISTER;
  id->auth_methods = EBUS_AUTH_PASSPHRASE | EBUS_AUTH_PRECONFIGURED;
  id->os_version = cfg->os_version;
  id->mac = cfg->mac;
  id->homie_domain = HOMIE_TOPIC_DOMAIN;
  id->homie_version = HOMIE_VERSION_NUM;
}

static void advertise_one(MdnsBackend* backend, uint8_t service, uint16_t port, TxtResult built,
                          const TxtRecord* rec, MdnsServicesResult* result) {
  result->txt[service] = (uint8_t)built;
  if (built != TXT_OK) {
    result->status[service] = MDNS_SERVICE_BUILD_FAILED;
    return;
  }
  bool ok = backend->advertise(mdns_service_type(service), port, rec->pairs, rec->count);
  result->status[service] = ok ? MDNS_SERVICE_ADVERTISED : MDNS_SERVICE_REFUSED;
}

void mdns_services_advertise(MdnsBackend* backend, const MdnsServicesConfig* cfg,
                             MdnsServicesResult* result) {
  EbusIdentity id;
  mdns_services_identity(cfg, &id);
  TxtRecord rec;

  advertise_one(backend, MDNS_SERVICE_DEVICE_INFO, cfg->http_port,
                txt_build_device_info(&id, &rec), &rec, result);

  EbusHttpInfo http = {MDNS_VAL_HTTP_PATH, MDNS_VAL_HTTP_VERSION, MDNS_VAL_HTTP_OPENAPI};
  advertise_one(backend, MDNS_SERVICE_HTTP, cfg->http_port, txt_build_http(&id, &http, &rec),
                &rec, result);

  if (cfg->log_port != 0) {
    advertise_one(backend, MDNS_SERVICE_LOG, cfg->log_port, txt_build_log(&id, &rec), &rec,
                  result);
  } else {
    result->status[MDNS_SERVICE_LOG] = MDNS_SERVICE_SKIPPED;
    result->txt[MDNS_SERVICE_LOG] = TXT_OK;
  }

  advertise_one(backend, MDNS_SERVICE_EBUS, cfg->ebus_port, txt_build_ebus(&id, &rec), &rec,
                result);
}
