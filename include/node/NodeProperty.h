#pragma once
#include <homie/Node.h>

class NodeProperty {
    public:
        NodeProperty(){};
        ~NodeProperty(){};

        void setup(const char* node_id, const char* homie_property_id);

        const char* homie_property_id()    const    {return _homie_property_id;}
        void set_homie_property_id (const char* prop_id);
        const char* value() const    {return _value;}
        const char* type()  const    {return _type;}
        const char* format()const    {return _format;}
        void add_on_change_callback();//self, callback: Callable);
        void add_on_set_callback();//self, callback: Callable);
        void remove_callback();//self, callback_id: uuid.UUID);
        void set_value_and_publish();//self, new_value: Any);
        void set_value_and_publish(int value);
        void set_value_and_publish(float value);
        void set_value_and_publish(const char* value);
        void set_value_and_publish(bool value);
        void set_value_and_publish(unsigned int value);
        void set_entity(); //self, new_value: Any);
        void set_entity_setter();//self, entity_setter: Callable);
        void as_dict();

        void set(int value);
        void set(float value);
        void set(const char* value);
        void set(bool value);
        void set(unsigned int value);
        void set(double value);

        void publish();
        const char* topic() const { return _topic; }
        void set_topic(const char* topic) { strcpy(_topic, topic); }

    private:
        char _homie_node_id[32] = {0};
        char _homie_property_id[32] = {0};
        char _type[16] = {0};
        char _format[64] = {0};
        char _topic[128] = {0};
        char _unit[8] = {0};
        int _round_to = 0;
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