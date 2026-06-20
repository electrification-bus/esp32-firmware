#pragma once
#include <homie/Node.h>
#include <platform/mqtt_client.h>
#include <platform/config.h>

class NodeProperty {
    public:
        NodeProperty(){};
        ~NodeProperty(){};

        void setup(const char* node_id, const char* property_id) {
            // Build topic + load the initial value against the entity's OWN device
            // (set by NodeEntity::setup() via g_active_setup_device), so a nested
            // child device's property values publish under the child's topic.
            Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
            set_homie_property_id(property_id);
            char _top[128] = {0};
            snprintf(_top, sizeof(_top), "%s%s/%s", dev->topic(), node_id, property_id);
            Serial.printf("NodeProperty: setup() topic: %s\n", _top);
            set_topic(_top);
            // Load initial value from the owning device (populated from homie.yml)
            Node* n = dev->getNode(node_id);
            if (n) {
                Property* p = n->getProperty(property_id);
                if (p) {
                    strncpy(_value, p->value(), sizeof(_value) - 1);
                    _value[sizeof(_value) - 1] = '\0';
                    _retained = p->retained();  // honor the property's retained flag (Homie 5 default: true)
                }
            }
        }

        void setup(const char* node_id, const char* property_id, entity_settable_callback_t cb, NodeEntity* ne) {
            setup(node_id, property_id);
            Device* dev = g_active_setup_device ? g_active_setup_device : &theDevice;
            char _top[128] = {0};
            snprintf(_top, sizeof(_top), "%s%s/%s/%s", dev->topic(), node_id, property_id, HOMIE_TOPIC_SET);
            entity_subscribe_for_callbacks(_top, cb, ne);
        }
        const char* homie_property_id()    const    {return _homie_property_id;}
        void set_homie_property_id(const char* id) {
            snprintf(_homie_property_id, sizeof(_homie_property_id), "%s", id);
        }
        const char* value() const    {return _value;}
        const char* type()  const    {return _type;}
        const char* format()const    {return _format;}
        void add_on_change_callback();//self, callback: Callable);
        void add_on_set_callback();//self, callback: Callable);
        void remove_callback();//self, callback_id: uuid.UUID);
        void set_value_and_publish() {
            // Implementation for publishing the current value
        }
        void set_value_and_publish(int value) {
            set(value);
            publish();
        }
        void set_value_and_publish(float value) {
            set(value);
            publish();
        }
        void set_value_and_publish(const char* value) {
            set(value);
            publish();
        }
        void set_value_and_publish(bool value) {
            set(value);
            publish();
        }
        void set_value_and_publish(unsigned int value) {
            set(value);
            publish();
        }
        void set_entity(); //self, new_value: Any);
        void set_entity_setter();//self, entity_setter: Callable);
        void as_dict();

        void set(int value) {
            _intValue = value;
            snprintf(_value, sizeof(_value), "%d", value);
        }   
        void set(float value) {
            _floatValue = value;
            snprintf(_value, sizeof(_value), "%.4f", value); //4 decimal precision
        }
        void set(const char* value) {
            snprintf(_stringValue, sizeof(_stringValue), "%s", value);
            snprintf(_value, sizeof(_value), "%s", value);
        }
        void set(bool value) {
            _boolValue = value;
            strcpy(_value, value ? "true" : "false");
        }
        void set(unsigned int value) {
            _unsignedValue = value;
            snprintf(_value, sizeof(_value), "%u", value);
        }
        void set(double value) {
            _doubleValue = value;
            snprintf(_value, sizeof(_value), "%f", value);
        }

        void publish() {
            // Publish the value via the thread-safe queue (any task/core). Homie 5
            // requires property values to be RETAINED so reconnecting controllers
            // get the last value; _retained mirrors the property's declared flag.
            mqtt_queue_publish(topic(), _value, _retained);
        }        
        const char* topic() const { return _topic; }
        void set_topic(const char* topic) { snprintf(_topic, sizeof(_topic), "%s", topic); }

    private:
        char _homie_node_id[32] = {0};
        char _homie_property_id[32] = {0};
        char _type[16] = {0};
        char _format[64] = {0};
        char _topic[128] = {0};
        char _unit[8] = {0};
        int _round_to = 0;
        bool _retained = true;   // Homie 5 default; set from the property in setup()
        bool _supports_target = false;
        Node* _parent_node = nullptr;
        //std::mutex _lock;
        char _value[32] = {0};
        int _intValue = 0;
        float _floatValue = 0.0;
        char _stringValue[32] = {0};
        bool _boolValue = false;
        unsigned int _unsignedValue = 0;
        double _doubleValue = 0;
};