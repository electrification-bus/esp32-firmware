#pragma once
#include <node/NodeEntity.h>

class SSR : public NodeEntity {
    public:
        SSR();
        ~SSR(){};

        void loop() override;
        void set(bool onOff) override;

        //const char* getNodeId() const override { return "relay1"; };
        bool getState() { return _state; };
        void set_on_state();
        void set_off_state();  
        void set_ssr_state(bool onoff);
        void entity_settable_callback(Property* property) override;
    private:
        NodeProperty _state_property;
        bool _state;
        bool LOW_TRIGGER = true;
};

extern SSR* ssr;