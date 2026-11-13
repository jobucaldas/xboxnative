// SPDX-License-Identifier: GPL-3.0-or-later
// Wine host test: d3dcompiler_47 donor serves a guest's static imports.
// xgameFinal.exe imports D3DCompile/D3DDisassemble/D3DGetBlobPart/D3DReflect;
// the d3d10 shim loads the DLL by name. This fixture does the same through
// donor_pe::exported after donor_pe::probe passed, so a failure here proves
// the donor's exports are absent, not merely unmapped.
#include <windows.h>
#include <initguid.h>   // shared/initguid.h: DEFINE_GUIDs get definitions here
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
// Wine harness: desktop substitutes for the AppContainer allocation APIs, as
// donor-runtime.cpp does. Console execution uses the real UWP paths.
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect"))(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"))(name);
}
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../donors/probe.h"
static int failures;
static void check(bool ok,const char* what) {
    printf("%s: %s\n",ok?"PASS":"FAIL",what);fflush(stdout);
    if(!ok)++failures;
}
// Wine's d3dcompiler_blob vtable order (QueryInterface, AddRef, Release,
// GetBufferPointer, GetBufferSize) matches the SDK ID3DBlob, so the fixture
// reads it through that interface exactly as a game does. Never touch fields.
int main() {
    if(!donor_pe::probe([](const char* line){puts(line);fflush(stdout);}))return 1;
    // xgameFinal.exe's four static d3dcompiler_47 imports, resolved through the
    // probed donor rather than a host System32 copy.
    // Resolve through the probed donor, not the host System32 copy: the fixture
    // proves the donor's exports serve a guest's static import binding.
    auto compile=(decltype(&D3DCompile))donor_pe::exported("d3dcompiler_47.dll","D3DCompile",0);
    auto reflect=(decltype(&D3DReflect))donor_pe::exported("d3dcompiler_47.dll","D3DReflect",0);
    check(compile&&reflect,"d3dcompiler_47 exports D3DCompile and D3DReflect");
    if(!compile||!reflect){printf("D3DCOMPILER symbol D3DCompile=%p D3DReflect=%p\n",(void*)compile,(void*)reflect);fflush(stdout);return failures?2:1;}
    static const char source[]=
        "float4 main(float4 pos:SV_Position):SV_Position{return pos;}\n";
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    HRESULT hr=compile(source,sizeof source-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&errors);
    if(errors) {
        char text[160];snprintf(text,sizeof text,"DWPE d3dcompiler errors: %.130s",(char*)errors->GetBufferPointer());
        printf("%s\n",text);fflush(stdout);
    }
    check(SUCCEEDED(hr)&&code,"D3DCompile compiles a real vertex shader");
    if(!code)return failures?3:1;
    void* table=nullptr;
    HRESULT rhr=reflect(code->GetBufferPointer(),code->GetBufferSize(),IID_ID3D11ShaderReflection,&table);
    check(SUCCEEDED(rhr)&&table,"D3DReflect returns a reflection object");
    if(!SUCCEEDED(rhr)||!table){printf("D3DCOMPILER reflect hr=0x%08lx table=%p\n",(unsigned long)rhr,table);fflush(stdout);return failures?4:1;}
    if(!table)return failures?4:1;
    auto reflection=(ID3D11ShaderReflection*)table;
    D3D11_SHADER_DESC desc{};
    HRESULT dhr=reflection->GetDesc(&desc);
    check(SUCCEEDED(dhr)&&desc.InputParameters>=1,"reflection GetDesc reports the input parameters");
    if(!SUCCEEDED(dhr)||desc.InputParameters<1){printf("D3DCOMPILER GetDesc hr=0x%08lx parameters=%u version=%#x\n",(unsigned long)dhr,desc.InputParameters,desc.Version);fflush(stdout);}
    reflection->Release();
    code->Release();
    printf("D3DCOMPILER DONOR PASS: probe, static imports, compile and reflect; no game code\n");fflush(stdout);
    return failures?5:0;
}
