#include "route.h"
#include "fake_factory.h"
#include "fake_swapchain.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct NativeOutput : IDXGIOutput {
    ULONG refs=1;
    IUnknown* parent=nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override {return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override {return parent?parent->QueryInterface(id,out):E_UNEXPECTED;}
    void STDMETHODCALLTYPE ReleaseOwnership() override {}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC* a) override {*a={};a->DesktopCoordinates.right=1920;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDisplayModeList(DXGI_FORMAT a,UINT b,UINT* c,DXGI_MODE_DESC* d) override {if(a!=DXGI_FORMAT_R8G8B8A8_UNORM || b!=7 || !c || d)return E_UNEXPECTED;*c=3;return DXGI_ERROR_MORE_DATA;}
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode(const DXGI_MODE_DESC* a,DXGI_MODE_DESC* b,IUnknown* c) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE WaitForVBlank() override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE TakeOwnership(IUnknown* a,BOOL b) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* a) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE SetGammaControl(const DXGI_GAMMA_CONTROL* a) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetGammaControl(DXGI_GAMMA_CONTROL* a) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE SetDisplaySurface(IDXGISurface* a) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData(IDXGISurface* a) override {return DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a) override {return DXGI_ERROR_UNSUPPORTED;}
};
struct NativeAdapter : IDXGIAdapter1 {
    ULONG refs=1;
    NativeOutput output;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IDXGIAdapter) && id!=__uuidof(IDXGIAdapter1))return E_NOINTERFACE;
        *out=static_cast<IDXGIAdapter1*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID,void**) override {return E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT index,IDXGIOutput** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;if(index)return DXGI_ERROR_NOT_FOUND;
        output.AddRef();*out=&output;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID,LARGE_INTEGER*) override {return E_NOTIMPL;}
};
struct RestrictedFactory : FakeFactory {
    IDXGIOutput* expected=nullptr;
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput* output,IDXGISwapChain1**) override {
        return output==expected?DXGI_ERROR_DEVICE_REMOVED:E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput* output,IDXGISwapChain1**) override {
        return output==expected?DXGI_ERROR_DEVICE_REMOVED:E_UNEXPECTED;
    }
};
struct OutputChain : FakeSwapChain {
    NativeOutput* output=nullptr;
    HRESULT result=S_OK;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override {
        if(!out)return DXGI_ERROR_INVALID_CALL;
        *out=nullptr;if(FAILED(result))return result;
        if(output){output->AddRef();*out=output;}return result;
    }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** out) override {return GetContainingOutput(out);}
};
int main() {
    NativeAdapter native;
    NativeOutput parent;
    auto adapter=new graphics_core::Adapter(&native,&parent);
    CHECK(adapter->EnumOutputs(0,nullptr)==DXGI_ERROR_INVALID_CALL);
    IDXGIOutput* output=reinterpret_cast<IDXGIOutput*>(1);
    CHECK(adapter->EnumOutputs(1,&output)==DXGI_ERROR_NOT_FOUND && !output);
    CHECK(adapter->EnumOutputs(0,&output)==S_OK && output!=&native.output);
    CHECK(native.output.refs==2);
    CHECK(graphics_core::native_output(output)==&native.output);
    CHECK(graphics_core::native_output(&parent)==&parent);
    CHECK(!graphics_core::native_output(nullptr));
    // Restricted outputs cross back to native DXGI at the presentation seam.
    RestrictedFactory nativeFactory;
    nativeFactory.expected=&native.output;
    {
        graphics_core::Factory factory(&nativeFactory);
        graphics_core::configure([](HWND)->BOOL{return TRUE;},
            [&](IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput* restriction,IDXGISwapChain1**)->HRESULT {
                return restriction==&native.output ? DXGI_ERROR_DEVICE_REMOVED : E_UNEXPECTED;
            });
        DXGI_SWAP_CHAIN_DESC1 desc{};
        // Exercise restriction translation with a structurally valid chain.
        desc.BufferCount=2;desc.SampleDesc.Count=1;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        IDXGISwapChain1* chain=reinterpret_cast<IDXGISwapChain1*>(1);
        CHECK(factory.CreateSwapChainForHwnd(&parent,reinterpret_cast<HWND>(1),&desc,nullptr,output,&chain)==DXGI_ERROR_DEVICE_REMOVED && !chain);
        CHECK(factory.CreateSwapChainForCoreWindow(&parent,&parent,&desc,output,&chain)==DXGI_ERROR_DEVICE_REMOVED);
        CHECK(factory.CreateSwapChainForComposition(&parent,&desc,output,&chain)==DXGI_ERROR_DEVICE_REMOVED);
        graphics_core::configure(nullptr,nullptr);
    }
    CHECK(adapter->Release()==1);
    IDXGIAdapter1* identity=nullptr;
    CHECK(output->GetParent(__uuidof(IDXGIAdapter1),(void**)&identity)==S_OK && identity==adapter);
    CHECK(identity->Release()==1);
    void* unknown=reinterpret_cast<void*>(1);
    CHECK(output->QueryInterface(__uuidof(IDXGIOutput1),&unknown)==E_NOINTERFACE && !unknown);
    CHECK(output->QueryInterface(__uuidof(IUnknown),&unknown)==S_OK && unknown==output);
    CHECK(output->Release()==1);
    DXGI_OUTPUT_DESC desc{};
    CHECK(output->GetDesc(&desc)==S_OK && desc.DesktopCoordinates.right==1920);
    UINT count=99;
    CHECK(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,7,&count,nullptr)==DXGI_ERROR_MORE_DATA && count==3);
    CHECK(output->WaitForVBlank()==DXGI_ERROR_UNSUPPORTED);
    CHECK(output->Release()==0);
    CHECK(native.output.refs==1 && native.refs==0 && parent.refs==1);
    CHECK(graphics_core::outputs.empty() && graphics_core::adapters.empty());
    NativeAdapter chainAdapter;
    chainAdapter.output.parent=&chainAdapter;
    RestrictedFactory nativeParent;
    auto factory=new graphics_core::Factory(&nativeParent);
    OutputChain nativeChain;
    nativeChain.output=&chainAdapter.output;
    auto chain=new graphics_core::SwapChain(&nativeChain,factory,nullptr);
    CHECK(factory->Release()==1);
    for(int restricted=0;restricted<2;++restricted) {
        auto query=[&](IDXGIOutput** out) {return restricted?chain->GetRestrictToOutput(out):chain->GetContainingOutput(out);};
        CHECK(query(nullptr)==DXGI_ERROR_INVALID_CALL);
        output=nullptr;
        CHECK(query(&output)==S_OK && output && output!=&chainAdapter.output);
        IDXGIAdapter* owner=nullptr;
        CHECK(output->GetParent(__uuidof(IDXGIAdapter),(void**)&owner)==S_OK && owner!=&chainAdapter);
        IDXGIFactory2* root=nullptr;
        CHECK(owner->GetParent(__uuidof(IDXGIFactory2),(void**)&root)==S_OK && root==factory);
        root->Release();owner->Release();output->Release();
        CHECK(chainAdapter.refs==1 && chainAdapter.output.refs==1);
        nativeChain.result=DXGI_ERROR_DEVICE_REMOVED;
        output=reinterpret_cast<IDXGIOutput*>(1);
        CHECK(query(&output)==DXGI_ERROR_DEVICE_REMOVED && !output);
        nativeChain.result=S_OK;
        nativeChain.output=nullptr;
        CHECK(query(&output)==S_OK && !output);
        nativeChain.output=&chainAdapter.output;
        chainAdapter.output.parent=nullptr;
        CHECK(query(&output)==E_UNEXPECTED && !output && chainAdapter.output.refs==1);
        chainAdapter.output.parent=&chainAdapter;
    }
    CHECK(chain->Release()==0 && nativeChain.refs==0 && nativeParent.refs==0);
    CHECK(graphics_core::outputs.empty() && graphics_core::adapters.empty());
    puts("graphics outputs: parent lifetime, identity, modes, native errors and unwrapping PASS");
}
