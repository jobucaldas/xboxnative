// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Guest GDI objects: memory DCs, bitmaps/DIB sections (real pixel memory),
// solid brushes, fonts (attributes only) and rectangle-list regions. Handles
// are registry entries, never host GDI handles. Window DCs (user32 GetDC
// records) have no surface here: drawing to them is refused, not faked.
// Semantics follow Wine 10 win32u (bitmap.c, dc.c, region.c, dib.c); text
// rasterization and colour profiles are not provided.
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>

namespace gdi32_core {
enum class Kind { Bitmap, Brush, Font, Region, MemoryDc, DisplayDc };
struct Object {
    Kind kind;
    bool stock=false;                 // default DC objects: shared, never freed
    // Bitmap: width/height/bpp, top-down rows of `stride` bytes at `bits`.
    int width=0,height=0,bpp=0,stride=0;bool topDown=false;BYTE* bits=nullptr;std::vector<BYTE> storage;bool section=false;
    COLORREF color=0;                 // brush
    LOGFONTW font{};                  // font
    std::vector<RECT> rects;          // region: disjoint rectangles
    Object* selectedBitmap=nullptr;   // memory DC
    HGDIOBJ selectedBrush=nullptr,selectedFont=nullptr;
    int selections=0;                 // bitmap selected into a DC
};
inline std::recursive_mutex objectsM;
inline std::map<void*,std::unique_ptr<Object>> objects;
inline Object* find(HGDIOBJ h,Kind kind) {
    auto it=objects.find(h);return it!=objects.end()&&it->second->kind==kind?it->second.get():nullptr;
}
inline Object* any(HGDIOBJ h){auto it=objects.find(h);return it==objects.end()?nullptr:it->second.get();}
inline HGDIOBJ add(std::unique_ptr<Object> o){void* h=o.get();objects.emplace(h,std::move(o));return (HGDIOBJ)h;}
// A new memory DC holds the stock 1x1 bitmap, white brush and system font, so
// the first SelectObject of each kind returns a real (stock) handle, as on Windows.
inline Object* stock(Kind kind) {
    static Object* objectsByKind[3]{};int i=kind==Kind::Bitmap?0:kind==Kind::Brush?1:2;
    if(!objectsByKind[i]) {
        auto o=std::make_unique<Object>();o->kind=kind;o->stock=true;
        if(kind==Kind::Bitmap){o->width=o->height=1;o->bpp=32;o->stride=4;o->topDown=true;o->storage.assign(4,0);o->bits=o->storage.data();}
        if(kind==Kind::Brush)o->color=RGB(255,255,255);
        if(kind==Kind::Font){wcscpy_s(o->font.lfFaceName,L"System");o->font.lfHeight=16;o->font.lfWeight=FW_BOLD;}
        objectsByKind[i]=o.get();add(std::move(o));
    }
    return objectsByKind[i];
}

// ---- Bitmaps. Only 32-bpp pixels are stored (what DIB sections and
// compatible bitmaps of this 32-bpp display use); other depths are refused.
inline Object* make_bitmap(int w,int h,int bpp,bool topDown) {
    if(w<=0||h<=0||w>16384||h>16384||bpp!=32){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    auto o=std::make_unique<Object>();o->kind=Kind::Bitmap;o->width=w;o->height=h;o->bpp=32;o->stride=w*4;o->topDown=topDown;
    o->storage.assign(size_t(o->stride)*h,0);o->bits=o->storage.data();
    auto raw=o.get();add(std::move(o));return raw;
}
inline HBITMAP WINAPI create_bitmap(int w,int h,UINT planes,UINT bpp,const void* init) {
    if(planes!=1){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=make_bitmap(w,h,int(bpp),true);if(!o)return nullptr;
    if(init)for(int y=0;y<h;++y)memcpy(o->bits+size_t(y)*o->stride,(const BYTE*)init+size_t(y)*((w*4+1)&~1),size_t(w)*4); // DDB rows are WORD aligned
    return (HBITMAP)o;
}
inline HBITMAP WINAPI create_compatible_bitmap(HDC,int w,int h) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    return (HBITMAP)make_bitmap(w,h,32,true);
}
inline HBITMAP WINAPI create_dib_section(HDC,const BITMAPINFO* info,UINT usage,void** bits,HANDLE section,DWORD) {
    if(bits)*bits=nullptr;
    if(!info||usage>1){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    if(section){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}   // file-mapping backed sections
    auto& hd=info->bmiHeader;
    if(hd.biSize<sizeof(BITMAPINFOHEADER)||hd.biPlanes!=1||(hd.biCompression!=BI_RGB&&hd.biCompression!=BI_BITFIELDS)){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=make_bitmap(hd.biWidth,hd.biHeight<0?-hd.biHeight:hd.biHeight,hd.biBitCount,hd.biHeight<0);if(!o)return nullptr;
    o->section=true;if(bits)*bits=o->bits;return (HBITMAP)o;
}
// GetDIBits for 32-bpp bitmaps: header query (bits==NULL) or row copy.
inline int WINAPI get_dibits(HDC,HBITMAP h,UINT start,UINT lines,void* out,BITMAPINFO* info,UINT usage) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto b=find(h,Kind::Bitmap);if(!b||!info||usage>1){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    auto& hd=info->bmiHeader;if(hd.biSize<sizeof(BITMAPINFOHEADER)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    if(!out||!hd.biBitCount) {
        hd.biWidth=b->width;hd.biHeight=b->topDown?-b->height:b->height;hd.biPlanes=1;hd.biBitCount=32;
        // 32-bpp BI_RGB is exactly this BGRX layout, and needs no colour masks
        // (callers often pass only a BITMAPINFOHEADER).
        hd.biCompression=BI_RGB;hd.biSizeImage=DWORD(b->stride*b->height);hd.biClrUsed=0;hd.biClrImportant=0;
        return 1;
    }
    if(hd.biBitCount!=32||hd.biWidth!=b->width){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
    const bool wantTopDown=hd.biHeight<0;UINT copied=0;
    for(UINT i=0;i<lines&&start+i<UINT(b->height);++i,++copied) {
        UINT row=start+i;   // DIB row index counted from the bottom unless top-down
        int src=b->topDown==wantTopDown?int(row):b->height-1-int(row);
        memcpy((BYTE*)out+size_t(i)*b->stride,b->bits+size_t(src)*b->stride,b->stride);
    }
    return int(copied);
}

// ---- DCs
inline HDC WINAPI create_compatible_dc(HDC) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=std::make_unique<Object>();o->kind=Kind::MemoryDc;
    o->selectedBitmap=stock(Kind::Bitmap);o->selectedBrush=stock(Kind::Brush);o->selectedFont=stock(Kind::Font);
    return (HDC)add(std::move(o));
}
// Only the display driver exists ("DISPLAY" or \\.\DISPLAY1).
inline HDC WINAPI create_dc_w(LPCWSTR driver,LPCWSTR device,LPCWSTR,const void*) {
    auto display=[](LPCWSTR s){return s&&(!_wcsicmp(s,L"DISPLAY")||!_wcsicmp(s,L"\\\\.\\DISPLAY1"));};
    if(!display(driver)&&!display(device)){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=std::make_unique<Object>();o->kind=Kind::DisplayDc;
    o->selectedBrush=stock(Kind::Brush);o->selectedFont=stock(Kind::Font);
    return (HDC)add(std::move(o));
}
inline BOOL WINAPI delete_dc(HDC h) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto d=any(h);if(!d||(d->kind!=Kind::MemoryDc&&d->kind!=Kind::DisplayDc)){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    if(d->selectedBitmap&&!d->selectedBitmap->stock)--d->selectedBitmap->selections;
    objects.erase(h);return TRUE;
}
inline HGDIOBJ WINAPI select_object(HDC dc,HGDIOBJ h) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto d=any(dc);auto o=any(h);
    if(!d||(d->kind!=Kind::MemoryDc&&d->kind!=Kind::DisplayDc)||!o){SetLastError(ERROR_INVALID_HANDLE);return nullptr;}
    switch(o->kind) {
    case Kind::Bitmap: {
        if(d->kind!=Kind::MemoryDc||(!o->stock&&o->selections&&d->selectedBitmap!=o))return nullptr;   // one DC at a time
        auto old=d->selectedBitmap;if(old&&!old->stock)--old->selections;d->selectedBitmap=o;if(!o->stock)++o->selections;
        return (HGDIOBJ)old;
    }
    case Kind::Brush:{auto old=d->selectedBrush;d->selectedBrush=h;return old;}
    case Kind::Font:{auto old=d->selectedFont;d->selectedFont=h;return old;}
    case Kind::Region:return (HGDIOBJ)(ULONG_PTR)(o->rects.empty()?NULLREGION:o->rects.size()==1?SIMPLEREGION:COMPLEXREGION);
    default:SetLastError(ERROR_INVALID_HANDLE);return nullptr;
    }
}
inline BOOL WINAPI delete_object(HGDIOBJ h) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=any(h);if(!o||o->kind==Kind::MemoryDc||o->kind==Kind::DisplayDc){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    if(o->stock)return TRUE;          // stock objects: deleting them is a successful no-op
    if(o->selections)return FALSE;   // still selected into a DC (Windows refuses as well)
    objects.erase(h);return TRUE;
}
inline Object* dc_bitmap(HDC dc){auto d=find(dc,Kind::MemoryDc);return d?d->selectedBitmap:nullptr;}
inline COLORREF WINAPI set_pixel(HDC dc,int x,int y,COLORREF c) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto b=dc_bitmap(dc);if(!b||x<0||y<0||x>=b->width||y>=b->height)return COLORREF(-1);
    int row=b->topDown?y:b->height-1-y;
    ((DWORD*)(b->bits+size_t(row)*b->stride))[x]=(GetRValue(c)<<16)|(GetGValue(c)<<8)|GetBValue(c);
    return c;
}
// SRCCOPY between memory DCs. A window DC has no surface in this backend.
inline BOOL WINAPI bit_blt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,DWORD rop) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto d=dc_bitmap(dst),s=dc_bitmap(src);
    if(!d||!s||rop!=SRCCOPY){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    for(int j=0;j<h;++j)for(int i=0;i<w;++i) {
        int dx=x+i,dy=y+j,px=sx+i,py=sy+j;
        if(dx<0||dy<0||dx>=d->width||dy>=d->height||px<0||py<0||px>=s->width||py>=s->height)continue;
        int dr=d->topDown?dy:d->height-1-dy,sr=s->topDown?py:s->height-1-py;
        ((DWORD*)(d->bits+size_t(dr)*d->stride))[dx]=((const DWORD*)(s->bits+size_t(sr)*s->stride))[px];
    }
    return TRUE;
}

// The UWP SDK hides DIBSECTION: its documented Win64 layout.
struct DibSection { BITMAP dsBm; BITMAPINFOHEADER dsBmih; DWORD dsBitfields[3]; HANDLE dshSection; DWORD dsOffset; };
static_assert(sizeof(DibSection)==104,"DIBSECTION layout");
// ---- GetObject: BITMAP (or DIBSECTION for sections), LOGBRUSH, LOGFONT.
// Unknown handles return 0 without touching the output (Wine win32u).
inline int get_object(HGDIOBJ h,int size,void* out,bool wide) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=any(h);if(!o)return 0;
    switch(o->kind) {
    case Kind::Bitmap: {
        BITMAP bm{0,o->width,o->height,o->stride,1,WORD(o->bpp),o->section?o->bits:nullptr};
        if(o->section&&size>=int(sizeof(DibSection))) {
            if(!out)return sizeof(DibSection);
            DibSection ds{};ds.dsBm=bm;auto& hd=ds.dsBmih;hd.biSize=sizeof hd;hd.biWidth=o->width;
            hd.biHeight=o->topDown?-o->height:o->height;hd.biPlanes=1;hd.biBitCount=32;hd.biCompression=BI_RGB;
            hd.biSizeImage=DWORD(o->stride*o->height);memcpy(out,&ds,sizeof ds);return sizeof ds;
        }
        if(!out)return sizeof bm;if(size<int(sizeof bm))return 0;
        memcpy(out,&bm,sizeof bm);return sizeof bm;
    }
    case Kind::Brush: {
        LOGBRUSH lb{BS_SOLID,o->color,0};
        if(!out)return sizeof lb;if(size<int(sizeof lb))return 0;
        memcpy(out,&lb,sizeof lb);return sizeof lb;
    }
    case Kind::Font: {
        // A font may be read partially (a prefix of LOGFONT), but never with a
        // non-positive size.
        if(out&&size<=0)return 0;
        if(wide){int n=std::min(size,int(sizeof(LOGFONTW)));if(!out)return sizeof(LOGFONTW);memcpy(out,&o->font,size_t(n));return n;}
        LOGFONTA a{};memcpy(&a,&o->font,offsetof(LOGFONTA,lfFaceName));
        WideCharToMultiByte(CP_ACP,0,o->font.lfFaceName,-1,a.lfFaceName,LF_FACESIZE,nullptr,nullptr);
        int n=std::min(size,int(sizeof a));if(!out)return sizeof a;memcpy(out,&a,size_t(n));return n;
    }
    default: return 0;
    }
}
inline int WINAPI get_object_a(HGDIOBJ h,int size,void* out){return get_object(h,size,out,false);}
inline int WINAPI get_object_w(HGDIOBJ h,int size,void* out){return get_object(h,size,out,true);}

// ---- Brushes, fonts (no rasterizer: text measurement is refused)
inline HBRUSH WINAPI create_solid_brush(COLORREF c) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=std::make_unique<Object>();o->kind=Kind::Brush;o->color=c;return (HBRUSH)add(std::move(o));
}
inline HFONT WINAPI create_font_indirect_w(const LOGFONTW* f) {
    if(!f){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto o=std::make_unique<Object>();o->kind=Kind::Font;o->font=*f;return (HFONT)add(std::move(o));
}
// Stock objects (Wine win32u/gdiobj.c stock table): the six brushes and the
// fonts, as registry objects created once and never deleted. Stock pens and
// the default palette have no object kind here and return NULL.
inline HGDIOBJ WINAPI get_stock_object(int index) {
    static const COLORREF brushes[]={RGB(255,255,255),RGB(192,192,192),RGB(128,128,128),RGB(64,64,64),RGB(0,0,0)};
    static HGDIOBJ table[20]={};
    std::lock_guard<std::recursive_mutex> l(objectsM);
    if(index<0||index>=20)return nullptr;
    if(table[index])return table[index];
    // A DC's default brush and font are these stock objects (WHITE_BRUSH, SYSTEM_FONT).
    if(index==0)return table[index]=(HGDIOBJ)stock(Kind::Brush);
    if(index==13)return table[index]=(HGDIOBJ)stock(Kind::Font);
    auto o=std::make_unique<Object>();o->stock=true;
    if(index<=4){o->kind=Kind::Brush;o->color=brushes[index];}
    else if(index==5||index==18){o->kind=Kind::Brush;o->color=index==18?RGB(255,255,255):0;} // NULL_BRUSH, DC_BRUSH
    else if((index>=10&&index<=14)||index==16||index==17){ // OEM/ANSI fixed/var, system, device default, system fixed, default GUI
        o->kind=Kind::Font;o->font.lfHeight=index==17?-11:16;o->font.lfWeight=index==12||index==16?700:400;
        o->font.lfCharSet=index==10?255:0;o->font.lfPitchAndFamily=(index==10||index==11||index==16)?1:2;
        wcscpy_s(o->font.lfFaceName,index==17?L"MS Shell Dlg":index==12||index==13?L"System":L"");
    } else return nullptr;
    return table[index]=add(std::move(o));
}
inline BOOL WINAPI get_text_metrics_w(HDC,void*){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
inline BOOL WINAPI get_text_extent_point_32a(HDC,LPCSTR,int,SIZE*){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
inline BOOL WINAPI get_text_extent_point_32w(HDC,LPCWSTR,int,SIZE*){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}   // no font rasterizer, as the A call

// ---- Regions as lists of disjoint rectangles: COPY, AND, OR and DIFF are
// exact on rectangle sets (as SetWindowRgn callers build them); XOR is refused.
inline bool empty(const RECT& r){return r.left>=r.right||r.top>=r.bottom;}
inline HRGN WINAPI create_rect_rgn(int l,int t,int r,int b) {
    std::lock_guard<std::recursive_mutex> lk(objectsM);
    auto o=std::make_unique<Object>();o->kind=Kind::Region;
    RECT rc{std::min(l,r),std::min(t,b),std::max(l,r),std::max(t,b)};if(!empty(rc))o->rects.push_back(rc);
    return (HRGN)add(std::move(o));
}
// Subtract b from a, appending the up-to-four remaining pieces.
inline void subtract(const RECT& a,const RECT& b,std::vector<RECT>& out) {
    RECT i{std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
    if(empty(i)){out.push_back(a);return;}
    if(a.top<i.top)out.push_back({a.left,a.top,a.right,i.top});
    if(i.bottom<a.bottom)out.push_back({a.left,i.bottom,a.right,a.bottom});
    if(a.left<i.left)out.push_back({a.left,i.top,i.left,i.bottom});
    if(i.right<a.right)out.push_back({i.right,i.top,a.right,i.bottom});
}
inline int WINAPI combine_rgn(HRGN dst,HRGN a,HRGN b,int mode) {
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto d=find(dst,Kind::Region),x=find(a,Kind::Region),y=mode==RGN_COPY?x:find(b,Kind::Region);
    if(!d||!x||!y){SetLastError(ERROR_INVALID_HANDLE);return ERROR;}
    std::vector<RECT> out;
    switch(mode) {
    case RGN_COPY:out=x->rects;break;
    case RGN_AND:for(auto& p:x->rects)for(auto& q:y->rects){RECT i{std::max(p.left,q.left),std::max(p.top,q.top),std::min(p.right,q.right),std::min(p.bottom,q.bottom)};if(!empty(i))out.push_back(i);}break;
    case RGN_OR:out=x->rects;for(auto& q:y->rects){std::vector<RECT> pieces{q};for(auto& p:x->rects){std::vector<RECT> next;for(auto& r:pieces)subtract(r,p,next);pieces.swap(next);}out.insert(out.end(),pieces.begin(),pieces.end());}break;
    case RGN_DIFF:out=x->rects;for(auto& q:y->rects){std::vector<RECT> next;for(auto& r:out)subtract(r,q,next);out.swap(next);}break;
    default:SetLastError(ERROR_NOT_SUPPORTED);return ERROR;   // RGN_XOR
    }
    d->rects=out;
    return out.empty()?NULLREGION:out.size()==1?SIMPLEREGION:COMPLEXREGION;
}

// ---- No OpenGL driver formats and no colour-management profile here.
inline int WINAPI get_pixel_format(HDC){SetLastError(ERROR_INVALID_PIXEL_FORMAT);return 0;}
inline int WINAPI describe_pixel_format(HDC,int,UINT,void*){return 0;}
inline BOOL WINAPI get_icm_profile_w(HDC,DWORD*,LPWSTR){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
}
