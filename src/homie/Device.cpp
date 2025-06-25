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
Device::Device() {
    _num_nodes = 0;
}

void Device::init(const char* name, const char* id, DeviceState state, PubSubClient* mqtt_client) {
  setName(name);
  setId(id);
  setState(state);
  setMQTTClient(mqtt_client);
  sprintf(_topic, "homie/5/%s/", id);
  
  //instantiate the nodes
  JsonDocument* nodes = get_node_config();
  JsonArray nodes_array = (*nodes)["nodes"].as<JsonArray>();
  for (JsonVariant node : nodes_array) {
    //Serial.printf("Device: adding node %s\n", node["name"].as<const char*>());
    addNode(node,_topic);
  }
}

Node* Device::addNode(JsonVariant node, const char* topic) {
    Node* n =  new Node();
    n->setDevice(this);
    n->setId(node["id"].as<const char*>());
    n->setName(node["name"].as<const char*>());
    n->setType(node["type"].as<const char*>());
    n->setTopic(topic);
    JsonVariant props = node["properties"].as<JsonArray>();
    if (props[0].is<JsonObject>()) {
      Property* prop = new Property();
      prop->setNode(n); // required: set the parent node on the property before anything else
      prop->setMQTTClient(_mqtt_client);
      JsonObject props_obj = props[0].as<JsonObject>();
      prop->from_dict(&props_obj); // Initialize property from JsonObject
      n->addProperty(prop);
      //Serial.printf("Device: Node: Prop '%s':'%s' val '%s'\n", prop->name(), prop->id(), prop->value());
    }
    Serial.printf("Device: Node: Adding '%s' with id '%s'\n", n->name(), n->id());
    _nodes[_num_nodes] = n;
    _num_nodes++;
    return n;
}

void Device::setState(DeviceState state) {
    _state = state;
}

void Device::setId(const char* id) {
    strcpy(_id,id);
    //SET-ID SIDE-EFFECT: set topic
    sprintf(_topic, "homie/5/%s/", _id);
}

void Device::setName(const char* name) {
    strcpy(_name,name);
}

void Device::setMQTTClient(PubSubClient* client) {
    _mqtt_client = client;
    for(int i=0; i < _num_nodes; i++) {
        _nodes[i]->setMQTTClient(client); // Set the MQTT client for each node
    }
}

const char* Device::topic() {
    return _topic;
}

String Device::toJson() {
    String json;
    serialize(json);
    return json;
}

char* Device::getId() {
    return _id;
}

// JSON serialization
void Device::serialize(String& output) {
  JsonDocument serialized;
    serialized["id"] = _id;
    serialized["name"] = _name;
    serialized["version"] = _version;
    serialized["state"] = device_state_to_cstr(_state);

    //nodes
    JsonDocument nodes;
    String node_serial;
    for(int i=0;i<_num_nodes;i++) {
      _nodes[i]->serialize(node_serial);
      nodes[_nodes[i]->id()] = node_serial;
    }
    serialized["nodes"] = nodes;
    serializeJson(serialized, output);
}

void Device::publish() {
  Serial.println("DEVICE publish: nodes");
  //nodes
  for (int i = 0; i < _num_nodes; i++) {
    //Serial.printf("DEVICE publish: node: %s\n", _nodes[i]->name());
    _nodes[i]->publish();
  }
  //device $description
  char top[128] = {0};
  sprintf(top, "%s$description", topic());
  String json = toJson();
  if (!_mqtt_client->publish(top, json.c_str())) {
      Serial.println("MQTT publish: failed");
  }
}