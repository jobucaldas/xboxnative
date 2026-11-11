// SPDX-License-Identifier: GPL-3.0-or-later
// In-game launcher overlay: View+Menu (Select+Start) opens a menu with Resume,
// Quit game, Force quit and an FPS counter toggle; the counter sits top-left.
// The game owns the CoreWindow swapchain, so the overlay is copied into the
// game's current back buffer just before its own Present (D3D11 games; other
// back-buffer kinds are left alone). Copies only: no pipeline state changes.
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include "host/d3d11_diag.h"   // the Present-failure path reports through its sink
#include "host/resize_drain.h" // the GPU drain the resize hook runs (tested directly)
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace guest_overlay {
enum Item { Resume, Quit, ForceQuit, Fps, ItemCount };
inline std::atomic<bool> menuOpen{false};
inline std::atomic<int> selection{Resume};
inline std::atomic<bool> fpsOn{false};
inline std::atomic<bool> busy{false};          // quitting: the menu shows a status line
inline std::mutex statusM;
inline std::string status;
inline void set_status(const std::string& s){std::lock_guard<std::mutex> l(statusM);status=s;}

// ---- CPU canvas (0xAARRGGBB), the launcher's 8x8 bitmap font
struct Canvas {
    int w=0,h=0;std::vector<uint32_t> px;
    void reset(int width,int height,uint32_t c){w=width;h=height;px.assign(size_t(w)*h,c);}
    void fill(int x,int y,int fw,int fh,uint32_t c) {
        for(int j=std::max(0,y);j<std::min(h,y+fh);++j)for(int i=std::max(0,x);i<std::min(w,x+fw);++i)px[size_t(j)*w+i]=c;
    }
    void text(int x,int y,const std::string& s,int sc,uint32_t c) {
        for(size_t k=0;k<s.size();++k){unsigned char ch=(unsigned char)s[k];if(ch>=128)ch='?';
            for(int r=0;r<8;++r){unsigned char row=(unsigned char)font8x8_basic[ch][r];
                for(int b=0;b<8;++b)if(row&(1<<b))fill(x+int(k)*8*sc+b*sc,y+r*sc,sc,sc,c);}}
    }
};
inline uint32_t rgb(int r,int g,int b){return 0xFF000000u|(r<<16)|(g<<8)|b;}

// ---- GPU side: one cached texture per panel, in the back buffer's format
struct Panel {
    winrt::com_ptr<ID3D11Texture2D> tex;ID3D11Device* device=nullptr;int w=0,h=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    std::vector<uint32_t> staging;
    void blit(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* target,DXGI_FORMAT fmt,bool rgba,
              const Canvas& c,int x,int y,int tw,int th) {
        if(x<0||y<0||x+c.w>tw||y+c.h>th)return;
        if(!tex||device!=dev||w!=c.w||h!=c.h||format!=fmt) {
            tex=nullptr;D3D11_TEXTURE2D_DESC d{};d.Width=c.w;d.Height=c.h;d.MipLevels=1;d.ArraySize=1;d.Format=fmt;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
            if(FAILED(dev->CreateTexture2D(&d,nullptr,tex.put())))return;
            device=dev;w=c.w;h=c.h;format=fmt;
        }
        staging.resize(c.px.size());
        for(size_t i=0;i<c.px.size();++i){uint32_t p=c.px[i];   // memory order: BGRA for 0xAARRGGBB, swap for RGBA
            staging[i]=rgba?((p&0xFF00FF00u)|((p>>16)&0xFF)|((p&0xFF)<<16)):p;}
        ctx->UpdateSubresource(tex.get(),0,nullptr,staging.data(),UINT(c.w*4),0);
        ctx->CopySubresourceRegion(target,0,UINT(x),UINT(y),0,tex.get(),0,nullptr);
    }
};
inline LARGE_INTEGER lastSample{},freq{};
inline Panel fpsPanel,menuPanel,graphPanel;
// ---- Frame timing: interval between the game's Presents and the time spent
// inside Present itself (blocking on the display/GPU queue). Kept for the
// on-screen graph and appended to LocalState\frametimes.csv by the launcher.
struct Frame { double atMs, frameMs, presentMs; };
inline std::mutex framesM;
inline std::vector<Frame> pending;          // not yet written to the CSV
inline std::array<float,240> history{};     // newest at historyPos-1
inline unsigned historyPos=0;
inline LARGE_INTEGER lastPresent{},firstPresent{};
inline double ms(LARGE_INTEGER a,LARGE_INTEGER b){return double(b.QuadPart-a.QuadPart)*1000.0/double(freq.QuadPart);}
inline void record(LARGE_INTEGER start,LARGE_INTEGER end) {
    if(!lastPresent.QuadPart){lastPresent=firstPresent=start;return;}
    Frame f{ms(firstPresent,start),ms(lastPresent,start),ms(start,end)};lastPresent=start;
    std::lock_guard<std::mutex> l(framesM);
    if(pending.size()<100000)pending.push_back(f);
    history[historyPos++%history.size()]=float(f.frameMs);
}
inline std::vector<Frame> take_frames(){std::lock_guard<std::mutex> l(framesM);std::vector<Frame> out;out.swap(pending);return out;}
inline unsigned framesSince=0;inline double fps=0;

inline const char* label(int item) {
    switch(item){case Resume:return "Resume";case Quit:return "Quit game";case ForceQuit:return "Force quit";
    default:return fpsOn?"FPS counter: On":"FPS counter: Off";}
}
// Whether the overlay can be drawn into this game's chain: D3D11 device and
// an 8-bit RGBA/BGRA back buffer. Probed at the first hooked Present.
inline std::atomic<bool> drawable{false};
inline std::atomic<bool> probed{false};
inline bool supported_format(DXGI_FORMAT f) {
    return f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||f==DXGI_FORMAT_B8G8R8A8_UNORM||
           f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||f==DXGI_FORMAT_B8G8R8X8_UNORM;
}
inline void probe(IDXGISwapChain* chain) {
    if(probed.exchange(true))return;
    winrt::com_ptr<ID3D11Device> dev;winrt::com_ptr<ID3D11Texture2D> bb;
    if(FAILED(chain->GetDevice(__uuidof(ID3D11Device),dev.put_void()))||FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),bb.put_void())))return;
    D3D11_TEXTURE2D_DESC d{};bb->GetDesc(&d);drawable=supported_format(d.Format);
}
inline void draw(IDXGISwapChain* chain) {
    probe(chain);
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(!freq.QuadPart){QueryPerformanceFrequency(&freq);lastSample=now;}
    ++framesSince;double dt=double(now.QuadPart-lastSample.QuadPart)/double(freq.QuadPart);
    if(dt>=0.5){fps=framesSince/dt;framesSince=0;lastSample=now;}
    const bool menu=menuOpen.load(),counter=fpsOn.load();
    if(!menu&&!counter)return;
    winrt::com_ptr<ID3D11Device> dev;if(FAILED(chain->GetDevice(__uuidof(ID3D11Device),dev.put_void())))return;
    winrt::com_ptr<ID3D11Texture2D> bb;if(FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),bb.put_void())))return;
    D3D11_TEXTURE2D_DESC d{};bb->GetDesc(&d);
    bool rgba;
    switch(d.Format){
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:rgba=true;break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8X8_UNORM:rgba=false;break;
    default:return;
    }
    winrt::com_ptr<ID3D11DeviceContext> ctx;dev->GetImmediateContext(ctx.put());
    const int sc=std::max(1,int(d.Height)/540);   // 2 at 1080p
    if(counter) {
        char b[32];snprintf(b,sizeof b,"FPS %.0f",fps);
        Canvas c;c.reset((8*8+8)*sc,12*sc,rgb(0,0,0));c.text(4*sc,2*sc,b,sc,rgb(120,255,140));
        fpsPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,c,8*sc,8*sc,int(d.Width),int(d.Height));
        // Frame-time graph: one column per frame (newest right), 0-50 ms tall;
        // guides at 16.7 ms (60 fps) and 33.3 ms (30 fps). Red: over 33 ms.
        const int gh=50,gw=int(history.size());
        Canvas g;g.reset(gw,gh,rgb(0,0,0));
        g.fill(0,gh-17,gw,1,rgb(60,90,60));g.fill(0,gh-34,gw,1,rgb(90,60,60));
        {std::lock_guard<std::mutex> l(framesM);
         for(int i=0;i<gw;++i){float v=history[(historyPos+i)%history.size()];int bar=std::min(gh,int(v+0.5f));
             g.fill(i,gh-bar,1,bar,v>33.4f?rgb(255,90,90):v>17.5f?rgb(255,210,63):rgb(120,255,140));}}
        Canvas big;big.reset(gw*sc,gh*sc,0);
        for(int y=0;y<big.h;++y)for(int x=0;x<big.w;++x)big.px[size_t(y)*big.w+x]=g.px[size_t(y/sc)*gw+x/sc];
        graphPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,big,8*sc,(8+14)*sc,int(d.Width),int(d.Height));
    }
    if(menu) {
        const int s=sc*2,rowH=14*s,pw=26*8*s,ph=rowH*(ItemCount+2)+8*s;
        Canvas c;c.reset(pw,ph,rgb(14,17,22));
        c.fill(0,0,pw,s,rgb(255,210,63));c.fill(0,ph-s,pw,s,rgb(255,210,63));c.fill(0,0,s,ph,rgb(255,210,63));c.fill(pw-s,0,s,ph,rgb(255,210,63));
        c.text(8*s,6*s,"Xboxnative",s,rgb(255,210,63));
        std::string line;{std::lock_guard<std::mutex> l(statusM);line=status;}
        for(int i=0;i<ItemCount;++i) {
            int y=6*s+rowH*(i+1);bool sel=i==selection.load();
            if(sel)c.fill(4*s,y-3*s,pw-8*s,rowH-2*s,rgb(60,70,90));
            c.text(8*s,y,label(i),s,busy?rgb(110,115,125):sel?rgb(255,255,255):rgb(170,180,195));
        }
        c.text(8*s,6*s+rowH*(ItemCount+1),line.empty()?"A select  B close":line,s/2>0?s/2:1,rgb(170,180,195));
        menuPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,c,(int(d.Width)-pw)/2,(int(d.Height)-ph)/2,int(d.Width),int(d.Height));
    }
}

// ---- Present hooks on the game's swapchain (vtable slots of IDXGISwapChain/1)
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
inline PresentFn realPresent=nullptr;inline Present1Fn realPresent1=nullptr;
// A failed Present is the one call the guest still makes after a device
// removal — everything else fails too, but Present keeps returning while the
// screen is frozen. The first failure asks the device why, once: without it
// the only trace is the caller's error hiding the removal.
inline std::atomic<bool> presentFailureLogged{false};
inline void presentFailure(IDXGISwapChain* c,HRESULT hr) {
    if(!presentFailureLogged.exchange(true)) {
        winrt::com_ptr<ID3D11Device> dev;
        if(SUCCEEDED(c->GetDevice(__uuidof(ID3D11Device),dev.put_void()))&&dev) {
            HRESULT reason=dev->GetDeviceRemovedReason();
            char b[128];snprintf(b,sizeof b,"guest Present failed hr=0x%08lx; device removed reason hr=0x%08lx",
                (unsigned long)hr,(unsigned long)reason);
            if(auto s=d3d11_diag::sink)s(b);
        }
    }
}
inline HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* c,UINT sync,UINT flags) {
    if(flags&DXGI_PRESENT_TEST)return realPresent(c,sync,flags);
    draw(c);LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
    HRESULT hr=realPresent(c,sync,flags);QueryPerformanceCounter(&b);record(a,b);
    if(FAILED(hr))presentFailure(c,hr);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1* c,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p) {
    if(flags&DXGI_PRESENT_TEST)return realPresent1(c,sync,flags,p);
    draw(c);LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
    HRESULT hr=realPresent1(c,sync,flags,p);QueryPerformanceCounter(&b);record(a,b);
    if(FAILED(hr))presentFailure(c,hr);
    return hr;
}
inline bool patch(void** slot,void* hook,void** original) {
    if(*slot==hook)return true;
    DWORD old;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    *original=*slot;*slot=hook;VirtualProtect(slot,sizeof(void*),old,&old);return true;
}
// ResizeBuffers keeps the chain as the launcher created it: the guest chain has
// no frame-latency waitable object (main.cpp strips it), and DXGI fails a resize
// whose flags add it (E_INVALIDARG). Unity asks for it on every resize; left
// failing, the chain kept its first size (Slime Rancher: an 800x600 chain under
// a 1920x1080 game, the menu drawn zoomed). Same rule for the buffer count:
// at least the three the chain was created with (0 keeps the current count).
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
inline ResizeFn realResize=nullptr;
inline std::atomic<unsigned> resizes{0};inline std::atomic<unsigned> lastResizeCount{0};inline std::atomic<long> lastResizeHr{0};
inline std::atomic<unsigned> lastResizeW{0},lastResizeH{0};
//
// A resize releases and recreates the back buffers. Real DXGI refuses it with
// DXGI_ERROR_INVALID_CALL while any reference is still outstanding, which is
// what forces apps to unbind first; a chain that tolerates outstanding
// references instead submits commands against freed buffers, and the
// d3d11on12 layer surfaces that to the driver as an invalid command — the
// Slime Rancher device reset (0x887a0007) hit between the 3rd and 4th resize,
// with Unity's deferred contexts still holding the old RTVs. So the hook
// unbinds the immediate context's render targets (the references that matter
// for buffer release; the game rebinds its targets every frame anyway — no
// ClearState, which would also wipe shader/IA state the engine caches on the
// CPU and only rebinds when dirty), then drains: end the event query, Flush
// (submits both the unbind and the query — a wait before this Flush would
// spin on a query the driver has not received while real work stays queued),
// then poll the query until in-flight work completes (bounded: a wedged
// device must not wedge the guest; the resize then fails honestly with the
// driver's error instead of adding a hang). Results pass through unchanged.
// The body lives in host/resize_drain.h so tests can assert the call order.
using DrainFn=void(*)(ID3D11Device*);
inline DrainFn realDrain=nullptr;   // test seam
inline void drain_device(ID3D11Device* dev) {
    if(realDrain){realDrain(dev);return;}
    resize_drain::drain(dev);   // shared body, order-asserted in tests/drain-order
}
inline HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain* c,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags) {
    // Experiment switch (LocalState\lockmode.txt, read at install): every
    // resize after the first returns S_OK without touching the chain. RCA
    // hypothesis: the device reset (0x887a0007) is triggered by the resize
    // path, so locking the mode should keep the game alive — at its first
    // resolution — if that is right. First resize still goes through so the
    // chain matches the window; the S_OK suppressions are counted and logged.
    if(lockFirstMode&&resizes.load()>0) {
        ++resizes;lastResizeCount=count;lastResizeW=w;lastResizeH=h;lastResizeHr=S_OK;
        if(auto s=d3d11_diag::sink)s("overlay: resize suppressed (lockmode), chain keeps its first mode");
        return S_OK;
    }
    winrt::com_ptr<ID3D11Device> dev;
    if(SUCCEEDED(c->GetDevice(__uuidof(ID3D11Device),dev.put_void()))&&dev)drain_device(dev.get());
    HRESULT hr=realResize(c,resize_count(count),w,h,f,resize_flags(flags));
    ++resizes;lastResizeCount=count;lastResizeW=w;lastResizeH=h;lastResizeHr=hr;return hr;
}
// IDXGISwapChain::Present is slot 8, ResizeBuffers 13; IDXGISwapChain1::Present1 22.
inline bool install(IDXGISwapChain1* chain) {
    void** vtable=*reinterpret_cast<void***>(chain);
    return patch(&vtable[8],(void*)&present,(void**)&realPresent)&&
           patch(&vtable[13],(void*)&resize,(void**)&realResize)&&
           patch(&vtable[22],(void*)&present1,(void**)&realPresent1);
}
// RCA experiment, opt-in via LocalState\lockmode.txt (read at the install site
// in main.cpp, on the launcher UI thread before any guest resize runs; the
// flag is only read on the guest thread afterwards, hence the atomic).
inline std::atomic<bool> lockFirstMode{false};

// ---- Input capture while the menu is open. The game reads its pads through
// Windows.Gaming.Input in this process; GetCurrentReading on the Gamepad and
// RawGameController classes (shared vtables) answers neutral to every thread
// but the launcher UI thread while blocked. The block lasts until all buttons
// are released after the menu closes, so a held B or A never reaches the game.
inline std::atomic<bool> inputBlocked{false};
inline DWORD uiThread=0;
inline unsigned long long blockedReads=0;
using GamepadRead=int32_t(__stdcall*)(void*,winrt::Windows::Gaming::Input::GamepadReading*);
using RawRead=int32_t(__stdcall*)(void*,uint32_t,bool*,uint32_t,int32_t*,uint32_t,double*,uint64_t*);
inline GamepadRead realGamepadRead=nullptr;inline RawRead realRawRead=nullptr;
// Raw axes have per-axis rest values (sticks 0.5, triggers 0). The last raw
// sample that was itself at rest (no button down, every axis within 0.1 of 0
// or 0.5) is replayed while blocked; judged from the same sample, so no
// other thread's reading can mark a moving stick as rest.
inline std::mutex rawM;inline std::vector<double> rawAxes;
inline double rest_value(double v){return v<0.25?0.0:0.5;}
inline bool at_rest(uint32_t nb,const bool* b,uint32_t na,const double* ax) {
    for(uint32_t i=0;b&&i<nb;++i)if(b[i])return false;
    for(uint32_t i=0;i<na;++i){double d=ax[i]-rest_value(ax[i]);if(d<-0.1||d>0.1)return false;}
    return true;
}
inline bool blocking(){return inputBlocked.load(std::memory_order_relaxed)&&GetCurrentThreadId()!=uiThread;}
inline int32_t __stdcall gamepad_read(void* self,winrt::Windows::Gaming::Input::GamepadReading* out) {
    int32_t hr=realGamepadRead(self,out);
    if(hr>=0&&out&&blocking()){auto stamp=out->Timestamp;*out={};out->Timestamp=stamp;++blockedReads;}
    return hr;
}
inline int32_t __stdcall raw_read(void* self,uint32_t nb,bool* b,uint32_t ns,int32_t* sw,uint32_t na,double* ax,uint64_t* ts) {
    int32_t hr=realRawRead(self,nb,b,ns,sw,na,ax,ts);
    if(hr>=0&&ax&&!blocking()&&at_rest(nb,b,na,ax)){std::lock_guard<std::mutex> l(rawM);rawAxes.assign(ax,ax+na);}
    if(hr>=0&&blocking()) {
        for(uint32_t i=0;b&&i<nb;++i)b[i]=false;
        for(uint32_t i=0;sw&&i<ns;++i)sw[i]=0;   // GameControllerSwitchPosition::Center
        std::lock_guard<std::mutex> l(rawM);
        // No rest sample yet: snap each axis to its nearer rest (trigger 0, stick 0.5).
        if(ax)for(uint32_t i=0;i<na;++i)ax[i]=rawAxes.size()==na?rawAxes[i]:rest_value(ax[i]);
        ++blockedReads;
    }
    return hr;
}
inline void block_input(bool on){inputBlocked=on;}
// IGamepad::GetCurrentReading is slot 8, IRawGameController::GetCurrentReading slot 13.
inline bool install_input_block(const winrt::Windows::Gaming::Input::Gamepad& pad) {
    uiThread=GetCurrentThreadId();
    void** gamepad=*reinterpret_cast<void***>(winrt::get_abi(pad));
    bool ok=patch(&gamepad[8],(void*)&gamepad_read,(void**)&realGamepadRead);
    try {
        auto raw=winrt::Windows::Gaming::Input::RawGameController::FromGameController(pad);
        if(raw){void** rv=*reinterpret_cast<void***>(winrt::get_abi(raw));ok=patch(&rv[13],(void*)&raw_read,(void**)&realRawRead)&&ok;}
    } catch(...) {ok=false;}
    return ok;
}

// ---- Graceful quit: WM_CLOSE to the game's visible top-level windows, as the
// desktop shell does when a window's close button is pressed.
inline unsigned close_guest_windows() {
    HMODULE records=LoadPackagedLibrary(L"uwph-u32c.dll",0);if(!records)return 0;
    using EnumProc=BOOL(CALLBACK*)(HWND,LPARAM);using Enum=BOOL(WINAPI*)(EnumProc,LPARAM);using Post=BOOL(WINAPI*)(HWND,UINT,WPARAM,LPARAM);using Visible=BOOL(WINAPI*)(HWND);
    auto enumWindows=(Enum)GetProcAddress(records,"EnumWindows");
    static Post post;static Visible visible;
    post=(Post)GetProcAddress(records,"PostMessageW");visible=(Visible)GetProcAddress(records,"IsWindowVisible");
    unsigned n=0;
    EnumProc each=[](HWND h,LPARAM count)->BOOL{
        if(!visible||visible(h)){if(post(h,WM_CLOSE,0,0))++*(unsigned*)count;}return TRUE;};
    if(enumWindows&&post)enumWindows(each,(LPARAM)&n);
    FreeLibrary(records);
    return n;
}
}
