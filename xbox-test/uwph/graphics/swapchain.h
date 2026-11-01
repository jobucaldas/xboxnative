#pragma once
#include <windows.h>
#include <dxgi1_2.h>
#include <new>
#include <atomic>
#include <functional>
#include <utility>
#include "diagnostics.h"
#include "adapter.h"
namespace graphics_core {
// Preserve the guest window identity without exposing record addresses to DXGI.
// The native chain still owns rendering, Present, buffers and resize validation.
struct SwapChain final : IDXGISwapChain1 {
    IDXGISwapChain1* real;
    IUnknown* factory;
    HWND window;
    std::function<void()> released;
    volatile LONG refs=1;
    std::atomic<unsigned long> submissions{0};
    SwapChain(IDXGISwapChain1* chain,IUnknown* parent,HWND h,std::function<void()> notify={})
        : real(chain),factory(parent),window(h),released(std::move(notify)) { factory->AddRef(); }
    ~SwapChain() {
        real->Release();
        // Reopen the presentation slot only after dropping our native reference.
        // Outstanding native buffers may still prevent DXGI recreation.
        if(released)released();
        factory->Release();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IDXGIObject) ||
           id==__uuidof(IDXGIDeviceSubObject) || id==__uuidof(IDXGISwapChain) ||
           id==__uuidof(IDXGISwapChain1)) {
            *out=static_cast<IDXGISwapChain1*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=InterlockedDecrement(&refs);if(!n)delete this;return n; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a,UINT b,const void* c) override { return real->SetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a,const IUnknown* b) override { return real->SetPrivateDataInterface(a,b); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a,UINT* b,void* c) override { return real->GetPrivateData(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override { return factory->QueryInterface(id,out); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out) override { return real->GetDevice(id,out); }
    HRESULT presented(HRESULT hr,UINT flags) {
        if(hr==S_OK && !(flags&DXGI_PRESENT_TEST)) {
            auto count=++submissions;
            if(count==1 || count%300==0)report("Present submissions",hr,count);
        } else if(FAILED(hr))report("Present failed",hr,flags);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags) override { return presented(real->Present(interval,flags),flags); }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void** out) override { return real->GetBuffer(index,id,out); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen,IDXGIOutput* output) override {
        // CoreWindow has no desktop exclusive mode. Leaving exclusive mode is
        // an idempotent request; entering it cannot be supported by this host.
        if(fullscreen)return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        return output?DXGI_ERROR_INVALID_CALL:S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen,IDXGIOutput** output) override {
        if(fullscreen)*fullscreen=FALSE;
        if(output)*output=nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override {
        if(!desc)return DXGI_ERROR_INVALID_CALL;
        HRESULT hr=real->GetDesc(desc);
        if(SUCCEEDED(hr)){desc->OutputWindow=window;desc->Windowed=TRUE;}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) override {
        return real->ResizeBuffers(count,width,height,format,flags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* mode) override {
        // Validate the guest argument before refusing desktop mode changes.
        // CoreWindow has no HWND target whose display mode DXGI can resize.
        return mode?DXGI_ERROR_UNSUPPORTED:DXGI_ERROR_INVALID_CALL;
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override {
        if(!out)return real->GetContainingOutput(out);
        *out=nullptr;
        IDXGIOutput* native=nullptr;
        HRESULT hr=real->GetContainingOutput(&native);
        if(FAILED(hr))return hr;
        // Preserve a successful empty native result without inventing an output.
        if(!native)return hr;
        HRESULT wrapped=wrap_chain_output(native,factory,out);
        return FAILED(wrapped)?wrapped:hr;
    }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* out) override { return real->GetFrameStatistics(out); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* out) override { return real->GetLastPresentCount(out); }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* out) override { return real->GetDesc1(out); }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* out) override {
        if(!out)return E_INVALIDARG;
        DXGI_SWAP_CHAIN_DESC desc{};
        HRESULT hr=real->GetDesc(&desc);
        if(FAILED(hr))return hr;
        out->RefreshRate=desc.BufferDesc.RefreshRate;
        out->ScanlineOrdering=desc.BufferDesc.ScanlineOrdering;
        out->Scaling=desc.BufferDesc.Scaling;
        out->Windowed=TRUE;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* out) override { if(!out)return E_INVALIDARG;*out=window;return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id,void** out) override { return real->GetCoreWindow(id,out); }
    HRESULT STDMETHODCALLTYPE Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters) override { return presented(real->Present1(interval,flags,parameters),flags); }
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return real->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** out) override {
        if(!out)return real->GetRestrictToOutput(out);
        *out=nullptr;
        IDXGIOutput* native=nullptr;
        HRESULT hr=real->GetRestrictToOutput(&native);
        if(FAILED(hr))return hr;
        // GetRestrictToOutput may succeed with no restriction.
        if(!native)return hr;
        HRESULT wrapped=wrap_chain_output(native,factory,out);
        return FAILED(wrapped)?wrapped:hr;
    }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* color) override { return real->SetBackgroundColor(color); }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* color) override { return real->GetBackgroundColor(color); }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation) override { return real->SetRotation(rotation); }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* rotation) override { return real->GetRotation(rotation); }
};
}
