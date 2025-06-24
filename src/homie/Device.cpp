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
    _id = id;
    _name = name;
    _version = version;

    //instantiate my nodes
    JsonDocument* nodes = get_node_config();
    JsonArray nodes_array = (*nodes)["nodes"].as<JsonArray>();
    for (JsonVariant node : nodes_array) {
      if (node) {
        Serial.printf("Device: adding node %s\n", node["name"].as<const char*>());
        Node* n = addNode(node);
        if (n) {
          n->setDevice(this); // Set the device for the node
          JsonVariant props = node["properties"].as<JsonArray>();
          if (props[0].is<JsonObject>()) {
            JsonObject props_obj = props[0].as<JsonObject>();
            Serial.printf("Device: Node: adding property %s\n", props_obj["id"].as<const char*>());
            Property* prop = new Property();
            prop->from_dict(&props_obj); // Initialize property from JsonObject
            prop->setMQTTClient(_mqtt_client);
            Serial.printf("Device: Node: property val %s\n", prop->value());
            n->addProperty(prop);
          }
        } else {
          Serial.printf("Failed to add node %s\n", node["id"].as<const char*>());
        }
      }
    }
}

Node* Device::addNode(JsonVariant node) {
    Node* n =  new Node();
    n->setId(node["id"].as<const char*>());
    n->setName(node["name"].as<const char*>());
    Serial.printf("Device: Node: Adding '%s' with id %s\n", n->name(), n->id());
    _nodes[_num_nodes++] = n;
    return n;
}

void Device::setState(DeviceState state) {
    _state = state;
}
void Device::setTopic(const char* topic) {
    _topic = topic;
}

void Device::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
    for(int i=0; i < _num_nodes; i++) {
        _nodes[i]->setMQTTClient(client); // Set the MQTT client for each node
    }
}

const char* Device::topic() {
    static char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/", HOMIE_TOPIC_PREFIX, getId());
    return topic;
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
  String topic = String(HOMIE_TOPIC_PREFIX) + "/" + _id + "/";\
  Serial.println("DEVICE publish: nodes");
  //nodes
  for (int i = 0; i < _num_nodes; i++) {
    _nodes[i]->publish(topic.c_str());
  }
  Serial.println("DEVICE publish: $description");
  //device $description
  topic.concat("/$description");
  String json = toJson();
  if (!_mqtt_client->publish(topic.c_str(), json.c_str())) {
      Serial.println("MQTT publish: failed");
  } else {
      Serial.printf("Published to %s: %s\n", topic.c_str(), json.c_str());
  }
}