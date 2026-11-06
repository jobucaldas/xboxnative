#pragma once
#include <dxgi1_2.h>
#include <atomic>
#include "../../shared/corewindow_factory.h"

// Play's guest HWNDs belong to the user32 record registry. Only this checked
// binding exposes the application's real CoreWindow to the native factory.
namespace play_graphics {
static IUnknown* hostWindow=nullptr;
static corewindow_dxgi::PresentSurface createSurface=nullptr;
static void (*event)(const char*)=nullptr;
static void configure(IUnknown* window,corewindow_dxgi::PresentSurface create,
                      void (*logger)(const char*)) {
    if(window)window->AddRef();
    if(hostWindow)hostWindow->Release();
    hostWindow=window;createSurface=create;event=logger;
}
// The guest window most recently bound to the CoreWindow; keyboard and mouse
// fall back to it when the game never sets focus or foreground itself.
static std::atomic<HWND> presented{nullptr};
static IUnknown* lookup(HWND window) {
    using Valid=BOOL(WINAPI*)(HWND);
    auto valid=(Valid)user32_core::resolve("user32.dll","IsWindow");
    if(!hostWindow || !valid || !valid(window))return nullptr;
    presented.store(window,std::memory_order_release);
    hostWindow->AddRef();return hostWindow;
}
static void result(HRESULT hr) {
    if(!event)return;
    char line[128];snprintf(line,sizeof line,"guest DXGI CoreWindow swapchain returned hr=0x%08lx",(unsigned long)hr);event(line);
}
static HRESULT factoryImpl(UINT flags,bool modern,REFIID id,void** out) {
    if(!out)return E_POINTER;*out=nullptr;
    IDXGIFactory2* native=nullptr;HRESULT hr;
    if(modern) {
        using Fn=HRESULT(WINAPI*)(UINT,REFIID,void**);
        auto fn=(Fn)mlp::resolve("dxgi.dll","CreateDXGIFactory2",0);
        hr=fn?fn(flags,__uuidof(IDXGIFactory2),(void**)&native):E_NOTIMPL;
    } else {
        using Fn=HRESULT(WINAPI*)(REFIID,void**);
        auto fn=(Fn)mlp::resolve("dxgi.dll","CreateDXGIFactory1",0);
        hr=fn?fn(__uuidof(IDXGIFactory2),(void**)&native):E_NOTIMPL;
    }
    if(FAILED(hr))return hr;
    auto proxy=new corewindow_dxgi::Factory(native,lookup,result,createSurface);
    hr=proxy->QueryInterface(id,out);proxy->Release();
    if(event)event("guest DXGI factory uses validated CoreWindow adapter");
    return hr;
}
static HRESULT WINAPI factory(REFIID id,void** out){return factoryImpl(0,false,id,out);}
static HRESULT WINAPI factory2(UINT flags,REFIID id,void** out){return factoryImpl(flags,true,id,out);}
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    if(ordinal||!name||_stricmp(dll,"dxgi.dll"))return 0;
    if(!strcmp(name,"CreateDXGIFactory")||!strcmp(name,"CreateDXGIFactory1"))return (uintptr_t)&factory;
    if(!strcmp(name,"CreateDXGIFactory2"))return (uintptr_t)&factory2;
    return 0;
}
}
