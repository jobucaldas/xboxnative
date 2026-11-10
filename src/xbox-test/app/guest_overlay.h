// SPDX-License-Identifier: GPL-3.0-or-later
// In-game launcher overlay: View+Menu (Select+Start) opens the Xbox guide
// (ui_guide.h): Resume, Quit game, Force quit, achievements, and an FPS
// counter with an optional frame-time graph, top-left. The game owns the
// CoreWindow swapchain, so the overlay is copied into the game's current back
// buffer just before its own Present (D3D11 games; other back-buffer kinds
// are left alone). Copies only: no pipeline state changes.
#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include "host/d3d11_diag.h"   // the Present-failure path reports through its sink
#include <winrt/base.h>
#include <winrt/Windows.Gaming.Input.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "ui_canvas.h"
#include "ui_font.h"
#include "ui_guide.h"

namespace guest_overlay {
enum Item { Resume, Quit, ForceQuit, Fps, ItemCount };
inline std::atomic<bool> menuOpen{false};
inline std::atomic<int> selection{Resume};
inline std::atomic<int> ui_guideTab{0};        // guide tab: 0 Home, 1 Achievements, 2 Settings
inline std::atomic<int> achTop{0};             // first visible achievement row
inline std::atomic<bool> achDirty{true};       // achievements snapshot needs a refresh
inline std::atomic<bool> fpsOn{false};
inline std::atomic<bool> graphOn{false};       // frame-time graph, drawn only when fpsOn too
inline std::atomic<bool> busy{false};          // quitting: the menu shows a status line
inline std::mutex statusM;
inline std::string status;
inline void set_status(const std::string& s){std::lock_guard<std::mutex> l(statusM);status=s;}

// ---- guide state: achievements snapshot + toasts, filled by the Steam thread
inline ui_guide::AchievementStore achievements;
inline std::mutex toastsM;
inline std::vector<ui_guide::Toast> toasts;   // queued; the oldest shows for kToastLifeSec
inline void push_toast(ui_guide::Toast t) {
    if (t.title.empty()) return;
    std::lock_guard<std::mutex> l(toastsM);
    if (toasts.size() < 4) toasts.push_back(std::move(t));
}
// The toast to draw right now: the oldest queued one, with entries past their
// 5 s lifetime dropped. `born` is seconds on the same clock the caller used
// when it filled Toast::born (the overlay's QPC seconds work for both).
inline constexpr double kToastLifeSec = 5.0;
inline ui_guide::Toast active_toast(double nowSeconds) {
    std::lock_guard<std::mutex> l(toastsM);
    while (!toasts.empty() && toasts.front().born > 0 &&
           nowSeconds - toasts.front().born > kToastLifeSec)
        toasts.erase(toasts.begin());
    return toasts.empty() ? ui_guide::Toast{} : toasts.front();
}

// ---- Selawik fonts for the guide, loaded once (caller passes the three TTF
// paths: regular, semibold, bold); without them the guide falls back to the
// 8x8 bitmap font.
inline std::mutex fontM;
inline ui::Font guideFont;
inline std::vector<uint8_t> fontData[3];       // keeps the TTF bytes alive for stb_truetype
inline bool fontTried = false;
inline bool load_fonts(const char* const* files, size_t n) {   // [regular, semibold, bold] TTF paths
    std::lock_guard<std::mutex> l(fontM);
    if (fontTried) return guideFont.ready();
    fontTried = true;
    bool ok = n >= 3;
    for (size_t i = 0; ok && i < 3; ++i) {
        FILE* f = std::fopen(files[i], "rb");
        if (!f) { ok = false; break; }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        fontData[i].resize(size_t(sz > 0 ? sz : 0));
        ok = sz > 0 && std::fread(fontData[i].data(), 1, fontData[i].size(), f) == fontData[i].size();
        std::fclose(f);
        if (ok) ok = guideFont.load(ui::Face(i), fontData[i].data(), fontData[i].size());
    }
    return ok;
}

// ---- legacy 8x8 fallback text (used only when the Selawik fonts are absent)
inline uint32_t rgb(int r,int g,int b){return ui::theme::argb(255,unsigned(r),unsigned(g),unsigned(b));}
// Draws `s` with the 8x8 bitmap font at `sc` pixels per cell. main.cpp includes
// font8x8_basic.h before this header, so the table is in scope.
inline void text8(ui::Canvas& c,int x,int y,const std::string& s,int sc,uint32_t col) {
    for(size_t k=0;k<s.size();++k){unsigned char ch=(unsigned char)s[k];if(ch>=128)ch='?';
        for(int r=0;r<8;++r){unsigned char row=(unsigned char)font8x8_basic[ch][r];
            for(int b=0;b<8;++b)if(row&(1<<b))c.fill_rect(float(x+int(k)*8*sc+b*sc),float(y+r*sc),float(sc),float(sc),col);}}
}
// A 8x8-only fallback "font" is not a ui::Font; without Selawik the guide
// simply draws its card flat (the caller's fallback), and the FPS counter
// falls back to the legacy 8x8 text path.
inline bool fonts_ready(){return guideFont.ready();}
inline std::vector<uint32_t> pxScratch;   // guide-card pixel buffer, reused per frame

// ---- GPU side: one cached texture per panel, in the back buffer's format
struct Panel {
    winrt::com_ptr<ID3D11Texture2D> tex;ID3D11Device* device=nullptr;int w=0,h=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    std::vector<uint32_t> staging;
    void blit(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* target,DXGI_FORMAT fmt,bool rgba,
              const ui::Canvas& c,int x,int y,int tw,int th) {
        if(x<0||y<0||x+c.width()>tw||y+c.height()>th)return;
        if(!tex||device!=dev||w!=c.width()||h!=c.height()||format!=fmt) {
            tex=nullptr;D3D11_TEXTURE2D_DESC d{};d.Width=c.width();d.Height=c.height();d.MipLevels=1;d.ArraySize=1;d.Format=fmt;
            d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
            if(FAILED(dev->CreateTexture2D(&d,nullptr,tex.put())))return;
            device=dev;w=c.width();h=c.height();format=fmt;
        }
        const uint32_t* px=c.data();
        staging.resize(c.width()*c.height());
        for(size_t i=0;i<staging.size();++i){uint32_t p=px[i];   // memory order: BGRA for 0xAARRGGBB, swap for RGBA
            staging[i]=rgba?((p&0xFF00FF00u)|((p>>16)&0xFF)|((p&0xFF)<<16)):p;}
        ctx->UpdateSubresource(tex.get(),0,nullptr,staging.data(),UINT(c.width()*4),0);
        ctx->CopySubresourceRegion(target,0,UINT(x),UINT(y),0,tex.get(),0,nullptr);
    }
};
inline LARGE_INTEGER lastSample{},freq{};
inline Panel fpsPanel,menuPanel,graphPanel;
// Staging pair for the 55% dim behind the guide card: a copy of the card's
// back-buffer region read back to the CPU, blended there, uploaded back.
inline winrt::com_ptr<ID3D11Texture2D> dimStaging;inline int dimW=0,dimH=0;inline DXGI_FORMAT dimFmt=DXGI_FORMAT_UNKNOWN;
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
// Dims the WHOLE game frame behind the guide card by 55% black, CPU-side:
// CopySubresourceRegion the full back buffer into a staging texture, Map(READ),
// blend (ui_guide::dim_row), UpdateSubresource back through a GPU texture in
// the back buffer's own format, then CopySubresourceRegion into the back
// buffer. The card then draws over the dimmed frame. Reuses the staging pair
// across frames; on any failure returns false and the caller draws the card
// over the undimmed frame (the old behaviour).
inline bool dim_frame(ID3D11Device* dev,ID3D11DeviceContext* ctx,ID3D11Texture2D* bb,
                      const D3D11_TEXTURE2D_DESC& d) {
    const int w=int(d.Width),h=int(d.Height);
    if(w<=0||h<=0)return false;
    if(!dimStaging||dimW!=w||dimH!=h||dimFmt!=d.Format) {
        D3D11_TEXTURE2D_DESC s{};s.Width=d.Width;s.Height=d.Height;s.MipLevels=1;s.ArraySize=1;
        s.Format=d.Format;                       // same format: CopySubresourceRegion is defined
        s.SampleDesc=d.SampleDesc;s.Usage=D3D11_USAGE_STAGING;s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        dimStaging=nullptr;
        if(FAILED(dev->CreateTexture2D(&s,nullptr,dimStaging.put())))return false;
        dimW=w;dimH=h;dimFmt=d.Format;
    }
    ctx->CopySubresourceRegion(dimStaging.get(),0,0,0,0,bb,0,nullptr);   // whole buffer
    D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(ctx->Map(dimStaging.get(),0,D3D11_MAP_READ,0,&m)))return false;
    // Scale the three colour bytes in place (RGBA and BGRA both: uniform scale),
    // then upload the same buffer through a GPU texture in the back buffer's format.
    for(int row=0;row<h;++row)
        ui_guide::dim_row(static_cast<uint8_t*>(m.pData)+size_t(row)*m.RowPitch,w,45);   // ~55% black scrim
    D3D11_TEXTURE2D_DESC up{};up=d;up.Usage=D3D11_USAGE_DEFAULT;up.CPUAccessFlags=0;up.BindFlags=0;
    winrt::com_ptr<ID3D11Texture2D> tex;
    if(FAILED(dev->CreateTexture2D(&up,nullptr,tex.put())))return false;
    ctx->UpdateSubresource(tex.get(),0,nullptr,m.pData,UINT(m.RowPitch),0);
    ctx->Unmap(dimStaging.get(),0);
    ctx->CopySubresourceRegion(bb,0,0,0,0,tex.get(),0,nullptr);   // whole frame back
    return true;
}

// Builds the guide view-model from the overlay's atomics and draws the card
// into a CPU canvas sized to the card, ready for menuPanel.blit at (80,32).
// The Canvas is a non-owning view over pxScratch, which the caller must keep
// alive until the blit has copied from it.
inline std::vector<uint32_t>& draw_guide_canvas(int sc,ui::Canvas& c) {
    std::string line;{std::lock_guard<std::mutex> l(statusM);line=status;}
    ui_guide::Model m;
    m.tab=ui_guideTab.load();m.sel=selection.load();
    m.fpsOn=fpsOn.load();m.graphOn=graphOn.load();m.busy=busy.load();m.status=line;
    static std::vector<ui_guide::AchievementView> snap;   // refreshed while open; stable pointer for draw
    if(achDirty.exchange(false)) snap=achievements.snapshot();
    m.achievements=&snap;
    m.achSel=selection.load();
    m.achTop=achTop.load();
    const int w=int(ui_guide::kCardW)*sc,h=int(ui_guide::kCardH)*sc;
    pxScratch.assign(size_t(w)*h,ui::theme::page);
    c=ui::Canvas(pxScratch.data(),w,h,w);
    if(guideFont.ready())ui_guide::draw_guide(c,guideFont,m);
    return pxScratch;
}

inline void draw(IDXGISwapChain* chain) {
    probe(chain);
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    if(!freq.QuadPart){QueryPerformanceFrequency(&freq);lastSample=now;}
    ++framesSince;double dt=double(now.QuadPart-lastSample.QuadPart)/double(freq.QuadPart);
    if(dt>=0.5){fps=framesSince/dt;framesSince=0;lastSample=now;}
    const double nowSec=double(now.QuadPart)/double(freq.QuadPart);   // QPC seconds, shared clock for toasts
    const bool menu=menuOpen.load(),counter=fpsOn.load();
    ui_guide::Toast toast;   // drawn with or without the menu open
    toast=active_toast(nowSec);   // locks toastsM itself
    if(!menu&&!counter&&toast.title.empty())return;
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
        // FPS counter + optional frame-time graph, one CPU canvas, top-left.
        const int gw=int(history.size());
        std::vector<float> hist(gw);
        unsigned head;
        {std::lock_guard<std::mutex> l(framesM);
         head=historyPos;
         for(int i=0;i<gw;++i)hist[size_t(i)]=history[(historyPos+unsigned(i))%history.size()];}
        const bool fonts=guideFont.ready();
        const int cw=240,ch=graphOn.load()?44+64:44;   // px at scale 1
        std::vector<uint32_t> buf(size_t(cw*sc)*size_t(ch*sc),0);
        ui::Canvas c(buf.data(),cw*sc,ch*sc,cw*sc);
        if(fonts) {
            ui_guide::draw_fps_counter(c,guideFont,fps,graphOn.load(),float(sc),hist.data(),unsigned(hist.size()),head);
        } else {   // no Selawik: legacy 8x8 text, no graph
            char b[32];std::snprintf(b,sizeof b,"FPS %.0f",fps);
            text8(c,4*sc,2*sc,b,sc,rgb(120,255,140));
        }
        fpsPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,c,8*sc,8*sc,int(d.Width),int(d.Height));
    }
    if(menu) {
        // 55% dim of the whole game frame behind the card, CPU-side; on any
        // failure the card draws over the undimmed frame (the old behaviour).
        dim_frame(dev.get(),ctx.get(),bb.get(),d);
        const int cardX=int(ui_guide::kCardX)*sc,cardY=int(ui_guide::kCardY)*sc;
        std::vector<uint32_t> cardBuf;
        ui::Canvas cardC(nullptr,0,0,0);
        draw_guide_canvas(sc,cardC);   // fills cardBuf; cardC views it
        menuPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,cardC,cardX,cardY,int(d.Width),int(d.Height));
    }
    if(!toast.title.empty()) {
        // Achievement toast: centred bottom pill, alive ~5 s regardless of the
        // menu (drawn whenever active_toast returns one).
        const int tw=int(ui_guide::kToastW)*sc,th=int(ui_guide::kToastH)*sc;
        const int tx=int(ui_guide::kToastX)*sc,ty=int(ui_guide::kToastY)*sc;
        std::vector<uint32_t> tbuf(size_t(tw)*size_t(th),0);
        ui::Canvas t(tbuf.data(),tw,th,tw);
        ui_guide::draw_toast(t,guideFont,toast,0,0);
        menuPanel.blit(dev.get(),ctx.get(),bb.get(),d.Format,rgba,t,tx,ty,int(d.Width),int(d.Height));
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
inline UINT resize_flags(UINT flags){return flags&~UINT(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);}
inline UINT resize_count(UINT count){return count==0?0:std::max<UINT>(count,3);}
inline HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain* c,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags) {
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
