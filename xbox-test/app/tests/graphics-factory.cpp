// Off-device contract test. The injected surface callback always fails; it
// records arguments and never represents a rendered frame or native HWND.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <cstdio>
#include "../../shared/corewindow_factory.h"
static unsigned checks=0,failures=0,calls=0,reports=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
struct Window final:IUnknown {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{return --refs;}
} window;
static HWND guestWindow=reinterpret_cast<HWND>(UINT_PTR(0x432100));
static IUnknown* lookup(HWND h){if(h!=guestWindow)return nullptr;window.AddRef();return &window;}
static IDXGIFactory2* expectedFactory;
static IUnknown* expectedDevice=reinterpret_cast<IUnknown*>(UINT_PTR(0x765400));
static DXGI_SWAP_CHAIN_DESC1 observed;
static HRESULT surface(IDXGIFactory2* real,IUnknown* device,IUnknown* core,
        const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput* restricted,IDXGISwapChain1** out){
    ++calls;CHECK(real==expectedFactory);CHECK(device==expectedDevice);
    CHECK(core==&window);CHECK(window.refs==2);CHECK(!restricted);CHECK(out&&!*out);
    observed=*desc;return E_ABORT;
}
static void report(HRESULT hr){++reports;CHECK(hr==E_ABORT);}
int main(){
    IDXGIFactory2* native=nullptr;
    HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory2),reinterpret_cast<void**>(&native));
    if(FAILED(hr)){printf("OFF-DEVICE factory unavailable hr=%08lx\n",(unsigned long)hr);return 2;}
    expectedFactory=native;
    auto proxy=new corewindow_dxgi::Factory(native,lookup,report,surface);
    void* queried=reinterpret_cast<void*>(UINT_PTR(1));
    CHECK(proxy->QueryInterface(__uuidof(ID3D11Device),&queried)==E_NOINTERFACE&&!queried);
    CHECK(proxy->QueryInterface(__uuidof(IUnknown),nullptr)==E_POINTER);
    CHECK(proxy->QueryInterface(__uuidof(IDXGIFactory),&queried)==S_OK&&queried==proxy);
    static_cast<IDXGIFactory*>(queried)->Release();
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=1920;desc.Height=1080;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(UINT_PTR(1));
    CHECK(proxy->CreateSwapChainForHwnd(expectedDevice,nullptr,&desc,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL&&!chain);
    CHECK(calls==0&&window.refs==1);
    CHECK(proxy->CreateSwapChainForHwnd(expectedDevice,guestWindow,&desc,nullptr,nullptr,nullptr)==E_POINTER);
    CHECK(proxy->CreateSwapChainForHwnd(expectedDevice,guestWindow,nullptr,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL&&!chain);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};fullscreen.Windowed=FALSE;
    CHECK(proxy->CreateSwapChainForHwnd(expectedDevice,guestWindow,&desc,&fullscreen,nullptr,&chain)==DXGI_ERROR_INVALID_CALL&&!chain);
    CHECK(calls==0&&window.refs==1);
    CHECK(proxy->CreateSwapChainForHwnd(expectedDevice,guestWindow,&desc,nullptr,nullptr,&chain)==E_ABORT&&!chain);
    CHECK(calls==1&&reports==1&&window.refs==1);
    CHECK(observed.Width==1920&&observed.Height==1080&&observed.SwapEffect==desc.SwapEffect);
    DXGI_SWAP_CHAIN_DESC legacy{};legacy.OutputWindow=guestWindow;legacy.Windowed=TRUE;
    legacy.BufferDesc.Width=640;legacy.BufferDesc.Height=480;legacy.BufferDesc.Format=desc.Format;
    legacy.SampleDesc.Count=1;legacy.BufferCount=1;legacy.BufferUsage=desc.BufferUsage;
    legacy.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* oldChain=nullptr;
    CHECK(proxy->CreateSwapChain(expectedDevice,&legacy,&oldChain)==E_ABORT&&!oldChain);
    CHECK(calls==2&&reports==2&&window.refs==1);
    CHECK(observed.BufferCount==2&&observed.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    CHECK(observed.Width==640&&observed.Height==480&&legacy.BufferCount==1);
    legacy.SampleDesc.Count=4;
    CHECK(proxy->CreateSwapChain(expectedDevice,&legacy,&oldChain)==DXGI_ERROR_INVALID_CALL&&!oldChain);
    legacy.SampleDesc.Count=1;legacy.Windowed=FALSE;
    CHECK(proxy->CreateSwapChain(expectedDevice,&legacy,&oldChain)==DXGI_ERROR_INVALID_CALL&&!oldChain);
    CHECK(calls==2&&reports==2&&window.refs==1);
    CHECK(proxy->Release()==0);
    printf("OFF-DEVICE CoreWindow factory contracts checks=%u failures=%u; no rendering claim\n",checks,failures);
    return failures?1:0;
}
