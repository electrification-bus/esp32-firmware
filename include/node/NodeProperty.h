#pragma once
#include <homie/Node.h>

class NodeProperty {
    public:
        NodeProperty(){};
        ~NodeProperty(){};

        const char* id()    const    {return _id;}
        const char* value() const    {return _value;}
        const char* type()  const    {return _type;}
        const char* format()const    {return _format;}
        void add_on_change_callback();//self, callback: Callable);
        void add_on_set_callback();//self, callback: Callable);
        void remove_callback();//self, callback_id: uuid.UUID);
        void set_value();//self, new_value: Any);
        void set_entity(); //self, new_value: Any);
        void set_entity_setter();//self, entity_setter: Callable);
        void as_dict();

    private:
        char _id[32] = {0};
        char _type[16] = {0};
        char _format[64] = {0};
        char _topic[128] = {0};
        char _unit[8] = {0};
        int _round_to = 0;
        bool _supports_target = false;
        Node* _parent_node = nullptr;
        //std::mutex _lock;
        char _value[32] = {0};
        //Any _entity;
        //Any _entity_setter;
        //std::map<uuid.UUID, Callable> _on_change_callbacks;
        //std::map<uuid.UUID, Callable> _on_set_callbacks;
};