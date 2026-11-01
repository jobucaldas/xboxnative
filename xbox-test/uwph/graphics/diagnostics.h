#pragma once
#include <windows.h>
#include <cstdio>
namespace graphics_core {
using Trace=void(*)(const char*);
inline Trace trace=nullptr;
inline void report(const char* operation,HRESULT result,unsigned long detail=0) {
    if(!trace)return;
    char line[192];std::snprintf(line,sizeof line,"GFXIN %s hr=0x%08lx detail=%lu",operation,(unsigned long)result,detail);
    trace(line);
}
}
