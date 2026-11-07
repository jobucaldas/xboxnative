#pragma once
#include <dxgi1_2.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
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
// Games also reach a factory through their device (IDXGIDevice::GetAdapter,
// then GetParent): that is the native factory d3d11 created for it, which
// knows nothing of guest windows. The native implementation identifies its
// factories by their vtable, so the object cannot be swapped or wrapped.
// Instead, once a guest device exists, three entries of that factory class's
// vtable are redirected: window association and HWND swapchain creation for a
// guest window go through the CoreWindow adapter above. Every other window,
// and every other method, reaches the original entry unchanged.
struct FactoryClass {void** vtable;void* associate;void* create;void* createForHwnd;};
static std::mutex classesM;
static std::vector<FactoryClass> classes;
static FactoryClass classOf(void* self) {
    std::lock_guard<std::mutex> lock(classesM);
    for(auto& c:classes) if(c.vtable==*(void***)self) return c;
    return {};
}
static bool guestWindow(HWND window) {
    using Valid=BOOL(WINAPI*)(HWND);
    auto valid=(Valid)user32_core::resolve("user32.dll","IsWindow");
    return window&&hostWindow&&valid&&valid(window);
}
template<class Call> static HRESULT throughAdapter(IUnknown* self,Call call) {
    IDXGIFactory2* native=nullptr;HRESULT hr=self->QueryInterface(__uuidof(IDXGIFactory2),(void**)&native);
    if(SUCCEEDED(hr)) {
        auto proxy=new corewindow_dxgi::Factory(native,lookup,result,createSurface);   // takes `native`
        hr=call(proxy);proxy->Release();
    }
    if(event){char line[128];snprintf(line,sizeof line,"guest device factory: guest-window swapchain hr=0x%08lx",(unsigned long)hr);event(line);}
    return hr;
}
using CreateSwapChainFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateForHwndFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
using AssociateFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,HWND,UINT);
static HRESULT STDMETHODCALLTYPE classAssociate(IDXGIFactory* self,HWND window,UINT flags) {
    if(guestWindow(window)) return S_OK;   // Alt+Enter handling: the console has no desktop modes
    auto c=classOf(self);return c.associate?((AssociateFn)c.associate)(self,window,flags):E_FAIL;
}
static HRESULT STDMETHODCALLTYPE classCreateSwapChain(IDXGIFactory* self,IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) {
    if(desc&&guestWindow(desc->OutputWindow)) {
        if(event){char line[200];snprintf(line,sizeof line,"guest legacy swapchain %ux%u format=%d buffers=%u effect=%d windowed=%d flags=0x%x samples=%u/%u",
            desc->BufferDesc.Width,desc->BufferDesc.Height,int(desc->BufferDesc.Format),desc->BufferCount,int(desc->SwapEffect),int(desc->Windowed),desc->Flags,desc->SampleDesc.Count,desc->SampleDesc.Quality);event(line);}
        return throughAdapter(self,[&](corewindow_dxgi::Factory* proxy){return proxy->CreateSwapChain(device,desc,out);});
    }
    auto c=classOf(self);return c.create?((CreateSwapChainFn)c.create)(self,device,desc,out):E_FAIL;
}
static HRESULT STDMETHODCALLTYPE classCreateForHwnd(IDXGIFactory2* self,IUnknown* device,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* output,IDXGISwapChain1** out) {
    if(guestWindow(window))
        return throughAdapter(self,[&](corewindow_dxgi::Factory* proxy){return proxy->CreateSwapChainForHwnd(device,window,desc,fullscreen,output,out);});
    auto c=classOf(self);return c.createForHwnd?((CreateForHwndFn)c.createForHwnd)(self,device,window,desc,fullscreen,output,out):E_FAIL;
}
static void adopt_device(IUnknown* device) {
    if(!device||!hostWindow)return;
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;IDXGIFactory2* factory=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&
       SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2),(void**)&factory))) {
        std::lock_guard<std::mutex> lock(classesM);
        void** vtable=*(void***)factory;bool known=false;
        for(auto& c:classes) known|=c.vtable==vtable;
        DWORD old=0;
        if(!known&&VirtualProtect(vtable+8,8*8,PAGE_READWRITE,&old)) {
            classes.push_back({vtable,vtable[8],vtable[10],vtable[15]});
            vtable[8]=(void*)&classAssociate;vtable[10]=(void*)&classCreateSwapChain;vtable[15]=(void*)&classCreateForHwnd;
            VirtualProtect(vtable+8,8*8,old,&old);
            if(event)event("guest device's DXGI factory: guest-window swapchains go through the CoreWindow adapter");
        }
    }
    if(factory)factory->Release();if(adapter)adapter->Release();if(dxgi)dxgi->Release();
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
