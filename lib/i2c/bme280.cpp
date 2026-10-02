#include <i2c/bme280.h>

bool BME280::setup() {
  I2CEntity::setup();  // Base class first: NodeEntity, then the shared bus

  temperature.setup(getNodeId(), "temperature");
  relative_humidity.setup(getNodeId(), "relative-humidity");
  air_pressure.setup(getNodeId(), "air-pressure");

  _since_last_read = millis();

  bool ok = begin(address(), &wire());
  if (ok) {
    _bme_temp     = getTemperatureSensor();
    _bme_pressure = getPressureSensor();
    _bme_humidity = getHumiditySensor();
  } else {
    // Retract rather than publish a sentinel; loop() stays idle until reboot.
    Serial.printf("BME280: not found at 0x%02X — polling suspended\n", address());
    temperature.clearValue();
    relative_humidity.clearValue();
    air_pressure.clearValue();
  }
  return ok;
}

bool BME280::pollIntervalTrigger() {
  if (millis() - _since_last_read > _read_interval) {
    _since_last_read = millis();
    return true;
  }
  return false;
}

void BME280::loop() {
  if (!_bme_temp || !pollIntervalTrigger()) return;

  sensors_event_t temp_event, pressure_event, humidity_event;
  _bme_temp->getEvent(&temp_event);
  _bme_pressure->getEvent(&pressure_event);
  _bme_humidity->getEvent(&humidity_event);

  // Units as declared in bme280.yml: °C, mb (Adafruit reports hPa, which is 1:1), %.
  temperature.set(temp_event.temperature);
  relative_humidity.set(humidity_event.relative_humidity);
  air_pressure.set(pressure_event.pressure);

  Serial.printf("BME280 '%s': %s °C, %s %%, %s mb\n", _homie_node_id,
                temperature.value(), relative_humidity.value(), air_pressure.value());

  temperature.publish();
  relative_humidity.publish();
  air_pressure.publish();
}
