#include "homie/Property.h"

Property::Property(const char* id,
        const char* name,
        const char* datatype)
        /*,
        const char* _format,
        bool _settable,
        void* _callback,
        bool _retained,
        const char* _unit,
        int _round_to,
        bool _supports_target,
        Node* _node,
        Device* _device,
        void* _async_loop)*/ {

        strncpy(_id, id, sizeof(_id) - 1);
        _id[sizeof(_id) - 1] = '\0'; // Ensure null termination

        strncpy(_name, name, sizeof(_name) - 1);
        _name[sizeof(_name) - 1] = '\0'; // Ensure null termination


        //strcpy(datatype,_datatype);
        //strcpy(format,_format);
     //   settable = _settable;
      //  callback = _callback;
     //   retained = _retained;
        //strcpy(unit, _unit); 
     //   round_to = _round_to;
     //   supports_target = _supports_target;
     //   node = _node;
     //   device = _device;
      //  async_loop = _async_loop;           
}

void Property::set_node(Node* node) {}

Node* Property::get_node() const {
    return nullptr;
}

const char* Property::id() const {
    return _id;
}

void Property::set_device(Device* device) {

}

bool Property::set_value(const char* value) {
    return false;
}

const char* Property::coerced_value() const {
    return "";
}



const char* Property::datatype() const {
    return "";
}

MqttClient* Property::mqtt_client() const {
    return nullptr;
}

void Property::start_mqtt_client() {}


bool Property::is_settable() const {
    return false;
}


bool Property::is_retained() const {
    return false;
}

bool Property::is_json_datatype() const {
    return false;
}
void Property::set_callback() const{
}

void Property::publish_target_value(const char* payload) {}

bool Property::publish_value() {
    return false;
}

void Property::description(SimpleMap<const char*, const char*>& desc) const {}

void Property::_settable_callback(const char* topic, const char* payload) {}

void Property::set_subscribe() {}