// SPDX-License-Identifier: GPL-3.0-or-later
#include "../host/presentation_restore.h"
#include <cstdio>
#include <string>
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Context {
    std::string& events;
    void ClearState(){events+="clear ";}
    void Flush(){events+="flush ";}
};
struct Resource {
    bool present;
    const char* name;
    std::string& events;
    Context* context=nullptr;
    explicit operator bool()const{return present;}
    Context* operator->(){return context;}
    void operator=(decltype(nullptr)){if(present)events+=name;present=false;}
};
int main(){
    std::string events;
    Context native{events};
    Resource device{true,"device ",events},context{true,"context ",events,&native};
    Resource chain{true,"chain ",events},buffer{true,"buffer ",events};
    unsigned calls=0;
    bool empty=false,success=false;
    auto initialize=[&]{
        ++calls;empty=!device&&!context&&!chain&&!buffer;
        events+="init ";device.present=context.present=true;
        if(success)chain.present=buffer.present=true;
        return success;
    };
    auto restore=[&](bool guest){return graphics_core::restore_launcher_presentation(
        guest,device,context,chain,buffer,initialize);};
    CHECK(restore(false) && calls==0 && events.empty());
    // Handoff has consumed the launcher's chain/buffer, not its device/context.
    chain.present=buffer.present=false;
    CHECK(restore(true) && calls==0 && events.empty());
    // Guest release permits restoration, but failure must prevent drawing.
    CHECK(!restore(false) && calls==1 && empty);
    CHECK(events=="clear flush context device init ");
    // A failed InitD3D left partial resources. Retry must release those too.
    events.clear();chain.present=true;success=true;
    CHECK(restore(false) && calls==2 && empty && buffer && chain);
    CHECK(events=="clear chain flush context device init ");
    events.clear();CHECK(restore(false) && calls==2 && events.empty());
    // With no surviving context, restoration must not dereference a null one.
    device.present=context.present=chain.present=buffer.present=false;
    CHECK(restore(false) && calls==3 && empty);
    CHECK(events=="init ");
    std::puts("presentation restore: guest isolation, release recovery, partial-init cleanup and retry PASS");
}
