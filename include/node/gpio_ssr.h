#pragma once
#include <node/NodeEntity.h>

class SSR : public NodeEntity {
    public:
        SSR();
        ~SSR(){};

        void loop() override;

        const char* getNodeName() const override { return "SSR"; };
        bool getState() { return _state; };
        void set_on_state();
        void set_off_state();  
        void set_ssr_state(bool onoff);
    private:
        bool _state;
        bool LOW_TRIGGER = true;
};

extern SSR* ssr;