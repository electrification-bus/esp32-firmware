#include <Arduino.h>
#include <ArduinoYaml.h>
#include <homie/Device.h>
#include <config.h>
#include <mqtt_client.h>
/*
  homie/5/device123/$state → ready
  homie/5/device123/$description → {
    "id": "device123",
    "homie": "5.0",
    "version": "12",
    "name": "My device",
    "nodes": {
      "mythermostat": {
        "name": "My thermostat",
        "type": "thermostat",
        "properties": {
          "temperature": {
            "name": "Temperature",
            "unit": "°C",
            "datatype": "integer",
            "settable": true
          }
        }
      }
    }
  }
*/
Device::Device(const char* id, const char* name, const char* version) {
    this->_id = id;
    this->_name = name;
    this->_version = version;

    //instantiate my nodes
    JsonDocument* nodes = get_node_config();
    JsonArray nodes_array = (*nodes)["nodes"].as<JsonArray>();
    for (JsonVariant node : nodes_array) {
      if (node) {
        Serial.printf("Processing node %s\n", node["name"].as<const char*>());
        Node* n = addNode(node);
        if (n) {
          n->setDevice(this); // Set the device for the node
          n->setMQTTClient(&mqttclient); // Set the MQTT client for the node
          JsonVariant props = node["properties"].as<JsonArray>();
          if (props[0].is<JsonObject>()) {
            JsonObject props_obj = props[0].as<JsonObject>();
            Serial.printf("Adding property %s w/format \n", props_obj["id"].as<const char*>());//, kv.value()["format"].as<const char*>());
            Property* prop = new Property(
                props_obj["id"].as<const char*>(),
                props_obj["name"].as<const char*>(),
                props_obj["datatype"].as<const char*>()
            );

            if (prop) {
              n->addProperty(prop);
            } else {
              Serial.printf("Failed to create property \n");
            }
          }
        } else {
          Serial.printf("Failed to add node %s\n", node["id"].as<const char*>());
        }
      }
    }
}

Node* Device::addNode(JsonVariant node) {
    // Create a new Node object from the JsonVariant
    //const char* id = node["id"].as<const char*>();
    //const char* name = node["name"].as<const char*>();
    Node* n =  new Node();
    n->setId(node["id"].as<const char*>());
    n->setName(node["name"].as<const char*>());
    Serial.printf("Adding node %s with name %s\n", n->id(), n->name());
    this->_nodes[_num_nodes++] = n;
    return n;
  
}

void Device::setState(DeviceState state) {
    this->_state = state;
}
void Device::setTopic(const char* topic) {
    this->_topic = topic;
}
String Device::toJson() {
    String json;
    serialize(json);
    return json;
}

String Device::getId() {
    return _id;
}

// JSON serialization
void Device::serialize(String& output) {
  JsonDocument serialized;
    serialized["id"] = _id;
    serialized["name"] = _name;
    serialized["version"] = _version;
    serialized["state"] = device_state_to_cstr(_state);
    serializeJson(serialized, output);
}

void Device::publish() {
    String json = toJson();
    String topic = String(HOMIE_TOPIC_PREFIX) + "/" + _id + "/$description";
    if (!mqttclient.publish(topic.c_str(), json.c_str())) {
        Serial.println("MQTT publish: failed");
    } else {
        Serial.printf("Published to %s: %s\n", topic.c_str(), json.c_str());
    }
}