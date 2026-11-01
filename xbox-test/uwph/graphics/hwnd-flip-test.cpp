#include "route.h"
#include "fake_factory.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static HWND window=reinterpret_cast<HWND>(0x1234);
static BOOL WINAPI valid(HWND h){return h==window;}
int main(){
    FakeFactory native;
    graphics_core::Factory factory(&native);
    unsigned calls=0;
    DXGI_SWAP_CHAIN_DESC1 seen{};
    graphics_core::configure(valid,[&](IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput*,IDXGISwapChain1**){
        ++calls;seen=*desc;return DXGI_ERROR_DEVICE_REMOVED;
    });
    auto device=reinterpret_cast<IUnknown*>(0x5678);
    for(auto effect:{DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL,
                     DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD}){
        const bool blit=effect==DXGI_SWAP_EFFECT_DISCARD || effect==DXGI_SWAP_EFFECT_SEQUENTIAL;
        for(UINT count:{blit?1u:2u,3u,16u}){
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width=1280;desc.Height=720;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count=1;desc.BufferCount=count;desc.SwapEffect=effect;
            desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
            auto original=desc;
            IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(1);
            auto before=calls;
            CHECK(factory.CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&chain)==DXGI_ERROR_DEVICE_REMOVED);
            CHECK(!chain && calls==before+1);
            auto expected=desc;
            if(blit){expected.BufferCount=std::max(2u,count);expected.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;}
            CHECK(!memcmp(&seen,&expected,sizeof seen));
            CHECK(!memcmp(&desc,&original,sizeof desc));
        }
    }
    // A blit request requiring an MSAA resolve or incompatible format must not
    // release launcher buffers or reach CoreWindow with an invalid flip desc.
    for(auto effect:{DXGI_SWAP_EFFECT_DISCARD,DXGI_SWAP_EFFECT_SEQUENTIAL}){
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferCount=1;desc.SwapEffect=effect;
        auto before=calls;
        for(auto sample: {DXGI_SAMPLE_DESC{4,0},DXGI_SAMPLE_DESC{1,1},DXGI_SAMPLE_DESC{0,0}}){
            desc.SampleDesc=sample;
            IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(1);
            CHECK(factory.CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
            CHECK(calls==before);
        }
        desc.SampleDesc={1,0};desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(1);
        CHECK(factory.CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&chain)==DXGI_ERROR_INVALID_CALL && !chain);
        CHECK(calls==before);
    }
    puts("graphics HWND: blit-to-flip conversion, native errors, unchanged input and pre-dispatch resolve/format refusal PASS");
}
