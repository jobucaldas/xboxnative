// SPDX-License-Identifier: GPL-3.0-or-later
// Timed keyboard injection commands for an active Play guest.
#pragma once
#include <cstdint>
#include <cstdlib>

namespace key_script {
struct Key {
    unsigned vk=0;
    uint32_t hold_ms=500;
};

enum class Edge { none, down, up };

inline uint32_t clamp_ms(unsigned long value) {
    return value>60000?60000:uint32_t(value);
}

inline bool number(const char*& p,unsigned long* value) {
    if(*p<'0'||*p>'9')return false;
    char* end=nullptr;
    *value=strtoul(p,&end,10);
    p=end;
    return true;
}

// `key <vk> [hold_ms]`; VKs are decimal and the hold defaults to 500 ms.
inline bool parse(const char* text,Key* out) {
    if(!text||!out)return false;
    while(*text==' '||*text=='\t')++text;
    unsigned long vk=0,hold=500;
    if(!number(text,&vk)||vk==0||vk>255)return false;
    if(*text==' '||*text=='\t') {
        while(*text==' '||*text=='\t')++text;
        if(*text&&!number(text,&hold))return false;
    }
    while(*text==' '||*text=='\t')++text;
    if(*text)return false;
    *out={unsigned(vk),clamp_ms(hold)};
    return true;
}

struct Runner {
    bool armed=false;
    bool down=false;
    Key key{};
    uint64_t phase_ms=0;
    void arm(Key value,uint64_t now_ms) {
        key=value;armed=true;down=false;phase_ms=now_ms;
    }
    void clear() {armed=false;down=false;key={};}
    Edge tick(uint64_t now_ms) {
        if(!armed)return Edge::none;
        if(!down){down=true;phase_ms=now_ms;return Edge::down;}
        if(now_ms-phase_ms<key.hold_ms)return Edge::none;
        clear();return Edge::up;
    }
};
}
