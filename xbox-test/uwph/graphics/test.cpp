#include "route.h"
#include "fake_factory.h"
#include "fake_swapchain.h"
#include <cstdio>
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
static HWND window=reinterpret_cast<HWND>(0x1234);
static unsigned calls;
static DXGI_SWAP_CHAIN_DESC1 observed;
static BOOL WINAPI valid(HWND h) { return h==window; }
static HRESULT create(IDXGIFactory2*,IUnknown*,HWND h,const DXGI_SWAP_CHAIN_DESC1* d,IDXGIOutput*,IDXGISwapChain1**) {
    if(h!=window)return E_UNEXPECTED;
    ++calls;observed=*d;return DXGI_ERROR_DEVICE_REMOVED;
}
int main() {
    FakeFactory native;
    // Scope verifies ownership: wrapper consumes exactly the factory reference.
    {
        graphics_core::Factory f(&native);
        void* p=reinterpret_cast<void*>(1);
        CHECK(f.QueryInterface(__uuidof(IDXGIFactory2),&p)==S_OK && p==&f);
        CHECK(f.Release()==1);
        CHECK(f.QueryInterface(__uuidof(IDXGISwapChain),&p)==E_NOINTERFACE && !p);
        CHECK(f.QueryInterface(__uuidof(IUnknown),nullptr)==E_POINTER);
        auto device=reinterpret_cast<IUnknown*>(0x5678);
        IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(1);
        DXGI_SWAP_CHAIN_DESC1 d{};d.BufferCount=2;d.SampleDesc.Count=1;
        d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
        graphics_core::configure(valid,nullptr);
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_UNSUPPORTED);
        graphics_core::configure(valid,create);
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_DEVICE_REMOVED);
        CHECK(calls==1 && observed.BufferCount==2);
        CHECK(f.CreateSwapChainForHwnd(device,nullptr,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL);
        CHECK(f.CreateSwapChainForHwnd(nullptr,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL);
        CHECK(f.CreateSwapChainForHwnd(device,window,nullptr,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL);
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,nullptr)==E_POINTER);
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,&fullscreen,nullptr,&chain)==DXGI_ERROR_INVALID_CALL);
        CHECK(calls==1);
        HWND h=reinterpret_cast<HWND>(1);
        CHECK(f.GetWindowAssociation(&h)==S_OK && !h);
        CHECK(f.MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER)==S_OK);
        CHECK(f.GetWindowAssociation(&h)==S_OK && h==window);
        CHECK(f.MakeWindowAssociation(window,8)==DXGI_ERROR_INVALID_CALL);
        CHECK(f.MakeWindowAssociation(reinterpret_cast<HWND>(9),0)==DXGI_ERROR_INVALID_CALL);
        CHECK(f.MakeWindowAssociation(nullptr,0)==S_OK);
        DXGI_SWAP_CHAIN_DESC legacy{};
        legacy.OutputWindow=window;legacy.Windowed=TRUE;legacy.BufferCount=1;
        legacy.SampleDesc.Count=1;legacy.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        IDXGISwapChain* old=reinterpret_cast<IDXGISwapChain*>(1);
        CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_DEVICE_REMOVED && !old);
        CHECK(calls==2 && observed.BufferCount==2 && observed.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
        CHECK(observed.AlphaMode==DXGI_ALPHA_MODE_IGNORE && observed.Scaling==DXGI_SCALING_STRETCH);
        legacy.SampleDesc.Count=4;
        CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL);
        legacy.SampleDesc.Count=1;legacy.Windowed=FALSE;
        CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL);
        CHECK(calls==2);
        legacy.Windowed=TRUE;
        // Validate guest counts before converting legacy presentation to flip.
        // Wine dxgi_validate_swapchain_desc: 1..16 for blit, 2..16 for flip.
        for(auto effect : {DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL,
                           DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            legacy.SwapEffect=effect;
            for(UINT count : {0u,17u,0xffffffffu}) {
                legacy.BufferCount=count;old=reinterpret_cast<IDXGISwapChain*>(1);
                CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL && !old);
                CHECK(calls==2);
            }
        }
        for(auto effect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            legacy.SwapEffect=effect;legacy.BufferCount=1;
            CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL && !old);
            CHECK(calls==2);
        }
        for(auto effect : {DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL,
                           DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            legacy.SwapEffect=effect;
            const bool flip=effect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
            for(UINT count : {flip?2u:1u,16u}) {
                legacy.BufferCount=count;const unsigned before=calls;
                CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_DEVICE_REMOVED && !old);
                CHECK(calls==before+1 && observed.BufferCount==std::max(2u,count));
                CHECK(observed.SwapEffect==(flip?effect:DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL));
            }
        }
        // Invalid modern descriptors must not dispatch to the UI hook, which
        // releases the launcher backbuffers before attempting native creation.
        const unsigned beforeInvalid=calls;
        for(UINT effect : {2u,5u,0xffffffffu}) {
            legacy.SwapEffect=static_cast<DXGI_SWAP_EFFECT>(effect);
            legacy.BufferCount=2;old=reinterpret_cast<IDXGISwapChain*>(1);
            CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL && !old);
            CHECK(calls==beforeInvalid);
        }
        for(UINT effect : {2u,5u,0xffffffffu}) {
            d.SwapEffect=static_cast<DXGI_SWAP_EFFECT>(effect);
            chain=reinterpret_cast<IDXGISwapChain1*>(1);
            CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
            CHECK(calls==beforeInvalid);
        }
        for(auto effect : {DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL,
                           DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            d.SwapEffect=effect;
            for(UINT count : {0u,17u,0xffffffffu}) {
                d.BufferCount=count;chain=reinterpret_cast<IDXGISwapChain1*>(1);
                CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
                CHECK(calls==beforeInvalid);
            }
        }
        for(auto effect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            d.SwapEffect=effect;d.BufferCount=1;
            CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
            d.BufferCount=2;d.SampleDesc.Count=4;
            CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
            d.SampleDesc.Count=1;d.SampleDesc.Quality=1;
            CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
            d.SampleDesc.Quality=0;
            CHECK(calls==beforeInvalid);
        }
        // Valid boundaries still reach native validation and preserve its error.
        for(auto effect : {DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL,
                           DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            d.SwapEffect=effect;
            const bool flip=effect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
            for(UINT count : {flip?2u:1u,16u}) {
                d.BufferCount=count;const unsigned before=calls;
                CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_DEVICE_REMOVED && !chain);
                CHECK(calls==before+1 && observed.BufferCount==std::max(2u,count));
                CHECK(observed.SwapEffect==(flip?effect:DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL));
            }
        }
        d.BufferCount=2;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        // Wine dxgi_validate_flip_swap_effect_format rejects these before
        // native creation. The UI hook must retain launcher buffers on error.
        for(auto effect : {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}) {
            const unsigned beforeFormats=calls;
            d.SwapEffect=effect;
            for(auto format : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                               DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_BC1_UNORM,
                               DXGI_FORMAT_D24_UNORM_S8_UINT,static_cast<DXGI_FORMAT>(0xffffffffu)}) {
                d.Format=format;chain=reinterpret_cast<IDXGISwapChain1*>(1);
                CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
                legacy.SwapEffect=effect;legacy.BufferCount=2;legacy.BufferDesc.Format=format;
                old=reinterpret_cast<IDXGISwapChain*>(1);
                CHECK(f.CreateSwapChain(device,&legacy,&old)==DXGI_ERROR_INVALID_CALL && !old);
                CHECK(calls==beforeFormats);
            }
            for(auto format : {DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R16G16B16A16_FLOAT,
                               DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM,
                               DXGI_FORMAT_B8G8R8A8_UNORM}) {
                d.Format=format;const unsigned before=calls;
                CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==DXGI_ERROR_DEVICE_REMOVED && !chain);
                CHECK(calls==before+1 && observed.Format==format);
            }
        }
        d.Format=DXGI_FORMAT_UNKNOWN;d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        FakeSwapChain nativeChain;
        unsigned released=0;bool releaseOrder=false;
        graphics_core::configure(valid,[&](IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1** out){
            *out=&nativeChain;return S_OK;
        },[&]{++released;releaseOrder=nativeChain.refs==0;});
        CHECK(f.CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&chain)==S_OK && chain);
        CHECK(f.refs==2);
        HWND guest=nullptr;
        CHECK(chain->GetHwnd(&guest)==S_OK && guest==window);
        DXGI_SWAP_CHAIN_DESC actual{};
        CHECK(chain->GetDesc(&actual)==S_OK && actual.OutputWindow==window && actual.Windowed);
        CHECK(actual.BufferDesc.Width==1280 && actual.BufferDesc.Height==720);
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC mode{};
        CHECK(chain->GetFullscreenDesc(nullptr)==E_INVALIDARG);
        CHECK(chain->GetFullscreenDesc(&mode)==S_OK && mode.Windowed);
        CHECK(mode.RefreshRate.Numerator==60000 && mode.RefreshRate.Denominator==1001);
        CHECK(mode.ScanlineOrdering==DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE && mode.Scaling==DXGI_MODE_SCALING_CENTERED);
        nativeChain.descResult=DXGI_ERROR_DEVICE_REMOVED;
        mode.RefreshRate.Numerator=123;
        CHECK(chain->GetFullscreenDesc(&mode)==DXGI_ERROR_DEVICE_REMOVED && mode.RefreshRate.Numerator==123);
        nativeChain.descResult=S_OK;
        CHECK(chain->GetParent(__uuidof(IDXGIFactory2),&p)==S_OK && p==&f);
        CHECK(f.Release()==2);
        BOOL exclusive=TRUE;IDXGIOutput* target=reinterpret_cast<IDXGIOutput*>(1);
        CHECK(chain->GetFullscreenState(&exclusive,&target)==S_OK && !exclusive && !target);
        // Wine dxgi/swapchain.c accepts either optional output, including neither.
        CHECK(chain->GetFullscreenState(nullptr,nullptr)==S_OK);
        target=reinterpret_cast<IDXGIOutput*>(1);
        CHECK(chain->GetFullscreenState(nullptr,&target)==S_OK && !target);
        exclusive=TRUE;
        CHECK(chain->GetFullscreenState(&exclusive,nullptr)==S_OK && !exclusive);
        CHECK(chain->SetFullscreenState(TRUE,nullptr)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
        CHECK(chain->SetFullscreenState(FALSE,nullptr)==S_OK);
        // Wine dxgi_swapchain_resize_target rejects a null descriptor first.
        CHECK(chain->ResizeTarget(nullptr)==DXGI_ERROR_INVALID_CALL);
        DXGI_MODE_DESC requested{};requested.Width=1920;requested.Height=1080;
        requested.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        CHECK(chain->ResizeTarget(&requested)==DXGI_ERROR_UNSUPPORTED);
        CHECK(requested.Width==1920 && requested.Height==1080);
        CHECK(chain->Present(2,DXGI_PRESENT_TEST)==DXGI_STATUS_OCCLUDED);
        CHECK(nativeChain.presents==1 && nativeChain.lastInterval==2 && nativeChain.lastFlags==DXGI_PRESENT_TEST);
        CHECK(chain->ResizeBuffers(2,1600,900,DXGI_FORMAT_UNKNOWN,0)==DXGI_ERROR_DEVICE_RESET);
        CHECK(nativeChain.lastWidth==1600 && nativeChain.lastHeight==900);
        CHECK(chain->QueryInterface(__uuidof(IDXGISwapChain1),&p)==S_OK && p==chain);
        CHECK(chain->Release()==1);
        CHECK(released==0);
        // Reconfiguration must not replace a live chain's captured release hook.
        graphics_core::configure(valid,nullptr);
        CHECK(chain->Release()==0 && nativeChain.refs==0 && f.refs==1);
        CHECK(released==1 && releaseOrder);
        graphics_core::configure(valid,nullptr);
    }
    CHECK(native.refs==0);
    CHECK(graphics_core::resolve("DXGI.DLL","CreateDXGIFactory2"));
    CHECK(!graphics_core::resolve("d3d11.dll","CreateDXGIFactory"));
    CHECK(!graphics_core::resolve(nullptr,"CreateDXGIFactory"));
    CHECK(!graphics_core::resolve("dxgi.dll",nullptr));
    CHECK(!graphics_core::resolve("dxgi.dll","CreateDXGIFactory9"));
    puts("graphics factory: ownership, HWND validation, dispatch, legacy flip conversion and failure propagation PASS");
}
