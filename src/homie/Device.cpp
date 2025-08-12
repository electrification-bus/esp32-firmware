#include <Arduino.h>
#include <ArduinoYaml.h>
#include <homie/homie.h>
#include <homie/Device.h>
#include <config.h>
#include <mqtt_client.h>
#include <util/jsonUtils.h>
#include <node/NodeManager.h>
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
    _state = DeviceState::DEVICE_STATE_INIT;
}

void Device::init(const char* name, const char* id, const char* type, PubSubClient* mqtt_client) {
  setName(name);
  setId(id);
  setType(type);
  _mqtt_client = mqtt_client;
  sprintf(_topic, "%s/%s/", HOMIE_TOPIC_PREFIX, id);
  
  //instantiate the nodes
  JsonDocument* nodes = get_node_config();
  JsonArray nodes_array = (*nodes)[HOMIE_NODES].as<JsonArray>();
  for (JsonVariant node : nodes_array) {
    //Serial.printf("Device: adding node %s\n", node["name"].as<const char*>());
    addNode(node,_topic);
  }
}

Node* Device::addNode(JsonVariant node, const char* topic) {
    Node* n =  new Node();
    n->setDevice(this);
    if (jsonExists(node[HOMIE_ID])) n->setId(node[HOMIE_ID].as<const char*>());
    if (jsonExists(node[HOMIE_NAME])) n->setName(node[HOMIE_NAME].as<const char*>());
    if (jsonExists(node[HOMIE_TYPE])) n->setType(node[HOMIE_TYPE].as<const char*>());
    n->setTopic(topic);
    JsonVariant props = node[HOMIE_PROPERTIES].as<JsonArray>();
    for(JsonVariant json_prop : props.as<JsonArray>()) {
      if (json_prop.is<JsonObject>()) {
        Property* prop = new Property();
        prop->setNode(n); // required: set the parent node on the property before anything else
        prop->setMQTTClient(_mqtt_client);
        JsonObject props_obj = json_prop.as<JsonObject>();
        prop->from_dict(&props_obj); // Initialize property from JsonObject
        n->addProperty(prop);
        if (prop->settable()) {
          prop->subscribe();
        }
      }
    }
    Serial.printf("Device: Node: Adding '%s' with id '%s'\n", n->name(), n->id());
    _nodes[_num_nodes++] = n;
    return n;
}

Node* Device::getNode(const char* id) {
  for (int i = 0; i < _num_nodes; i++) {
      if (strcmp(_nodes[i]->id(), id) == 0) {
          return _nodes[i];
      }
  }
  Serial.printf("Device: Node '%s' not found\n", id);
  return nullptr;
}

void Device::setState(DeviceState state) {
  DeviceState previous_state = _state;
  _state = state;

  //state change?
  if (_state != previous_state) {
    publishState();
  }
}

void Device::setId(const char* id) {
    strcpy(_id,id);
    //SET-ID SIDE-EFFECT: set topic
    sprintf(_topic, "%s/%s/", HOMIE_TOPIC_PREFIX, _id);
}

void Device::setName(const char* name) {
    strcpy(_name,name);
}

void Device::setType(const char* type) {
    strcpy(_type, type);
}

const char* Device::type() {
    return _type;
}

void Device::setMQTTClient(PubSubClient* client) {
  _mqtt_client = client;
  for(int i=0; i < _num_nodes; i++) {
      _nodes[i]->setMQTTClient(client); // Set the MQTT client for each node
  }
}

void Device::mqttConnected() {
  for(int i=0;i<_num_nodes;i++) {
    _nodes[i]->mqttConnected();
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
void Device::serialize(String& serialized) {
  JsonDocument json;
  json[HOMIE_NAME] = _name;
  json[HOMIE_TYPE] = _type;

  //nodes
  JsonDocument json_nodes;
  for(int i=0;i<_num_nodes;i++) {
    json_nodes[_nodes[i]->id()] = _nodes[i]->serialize();
  }
  json[HOMIE_NODES] = json_nodes;
  serializeJson(json, serialized);
}

void Device::publishState() {
  Serial.println("DEVICE publish: $state");
  char state[128] = {0};
  sprintf(state, "%s%s", topic(), HOMIE_$STATE);

  if (!_mqtt_client->publish(state, device_state_to_cstr(_state), true)) {
      Serial.println("MQTT publish: $state failed");
  }
}

void Device::publish() {

  //$description
  Serial.println("DEVICE publish: $description");
  char top[128] = {0};
  sprintf(top, "%s%s", topic(), HOMIE_$DESCRIPTION);
  String json = toJson();
  if (!_mqtt_client->publish(top, json.c_str(), true)) {
      Serial.println("MQTT publish: $description failed");
  }

  //nodes
  Serial.println("DEVICE publish: nodes");
  for (int i = 0; i < _num_nodes; i++) {
    Serial.printf("DEVICE publish: node: %s\n", _nodes[i]->name());
    _nodes[i]->publish();
  }

}