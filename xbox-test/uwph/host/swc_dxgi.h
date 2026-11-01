#pragma once
#include "../../shared/corewindow_factory.h"
// UI-thread DXGI factory route. Never send an opaque w32u HWND to system DXGI.
namespace swc {
static IUnknown* (*corewindow)(HWND);
static HMODULE display;
using Factory = corewindow_dxgi::Factory;
static HRESULT WINAPI factory(REFIID id,void** out) {
    if(!out)return E_POINTER; *out=nullptr;
    IDXGIFactory2* real=nullptr; HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory2),(void**)&real);
    if(FAILED(hr))return hr;
    auto f=new Factory(real,corewindow,[](HRESULT value){logf("SWC route HWND -> CoreWindow hr=0x%08lx",(unsigned long)value);}); hr=f->QueryInterface(id,out); f->Release(); return hr;
}
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    if(!name || ordinal)return 0;
    if(!_stricmp(dll,"user32.dll"))return (uintptr_t)GetProcAddress(display,name);
    if(!_stricmp(dll,"dxgi.dll") && (!strcmp(name,"CreateDXGIFactory1") || !strcmp(name,"CreateDXGIFactory")))return (uintptr_t)&factory;
    if(!_stricmp(dll,"d3d11.dll") && !strcmp(name,"D3D11CreateDevice"))return (uintptr_t)&D3D11CreateDevice;
    for(auto& b:wxl::builtins)if(!_stricmp(dll,b.dll)&&!strcmp(name,b.name))return b.address;
    return 0;
}
static void run(IUnknown* window,const wchar_t* path) {
    display=LoadPackagedLibrary(L"uwph-user32.dll",0);
    using Init=void(*)(IUnknown*,void(*)(),LONG,LONG);
    auto init=display?(Init)GetProcAddress(display,"uwph_display_init"):nullptr;
    corewindow=display?(IUnknown*(*)(HWND))GetProcAddress(display,"uwph_corewindow"):nullptr;
    if(!init || !corewindow){logf("SWC FAIL display exports");return;}
    init(window,[]{ CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent); },1920,1080);
    // Guest callbacks and retained D3D objects live until process termination.
    static wxl::Image image;
    if(!wxl::run(path,test_log,&image,resolve)) {logf("SWC FAIL mapping");return;}
    unsigned rwx=0;
    for(size_t offset=0;offset<image.size;offset+=4096) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(image.base+offset,&info,sizeof(info))) {logf("SWC FAIL VirtualQuery");return;}
        if((info.Protect & 0xff)==PAGE_EXECUTE_READWRITE || (info.Protect & 0xff)==PAGE_EXECUTE_WRITECOPY)++rwx;
    }
    logf("SWC mapped pages=%zu RWX=%u",image.size/4096,rwx);
    if(rwx)return;
    auto dos=(IMAGE_DOS_HEADER*)image.base;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+dos->e_lfanew);
    DWORD result=((DWORD(WINAPI*)())(image.base+nt->OptionalHeader.AddressOfEntryPoint))();
    logf("SWC RESULT entry=%lu failures=%d",result,result==42?0:1);
}
}
