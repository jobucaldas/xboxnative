#include "../gdi32/device_caps.h"
// Record-only user32 subset. No OS HWND, CoreWindow, device notifications or rendering.
// Wine user32/*.spec, user32/win.c and win32u/{class,window}.c define the ABI/semantics.
#include <windows.h>
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include "keymap.h"
#include "corewindow_feed.h"
// The APP family this DLL compiles under (build.sh --uwp) hides several
// desktop-only SDK declarations this record module still needs to export
// (they are plain data layouts/constants, not calls to restricted entry
// points: this DLL defines them, it never calls the real ones). Mirror the
// documented layouts locally rather than fight WINAPI_FAMILY visibility.
#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4
#endif
#ifndef KEYEVENTF_EXTENDEDKEY
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002
#define KEYEVENTF_UNICODE 0x0004
#define KEYEVENTF_SCANCODE 0x0008
#endif
#ifndef ENUM_CURRENT_SETTINGS
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)
#endif
struct WindowPlacement2 { UINT length,flags,showCmd; POINT ptMinPosition,ptMaxPosition; RECT rcNormalPosition; };
struct RawInputDevice2 { unsigned short usUsagePage,usUsage; DWORD dwFlags; HWND hwndTarget; };
struct RawInputDeviceList2 { HANDLE hDevice; DWORD dwType; };
struct RawInputHeader2 { DWORD dwType,dwSize; HANDLE hDevice; UINT_PTR wParam; };
static_assert(sizeof(WindowPlacement2)==44,"WINDOWPLACEMENT layout");
static_assert(sizeof(RawInputDevice2)==16,"RAWINPUTDEVICE layout");
static_assert(sizeof(RawInputDeviceList2)==16,"RAWINPUTDEVICELIST layout");
namespace u32c {
using Proc=LRESULT(CALLBACK*)(HWND,UINT,WPARAM,LPARAM);
struct ClassEx {UINT size,style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCWSTR menu,name;HICON smallIcon;};
struct ClassW {UINT style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCWSTR menu,name;};
struct Create {void* param;HINSTANCE instance;HMENU menu;HWND parent;int cy,cx,y,x;LONG style;LPCWSTR title,name;DWORD ex;};
static_assert(sizeof(ClassEx)==80 && sizeof(Create)==80,"Win64 ABI");
struct Class {ClassEx info;std::wstring name,menu;ATOM atom;std::vector<BYTE> extra;bool ansi=false;}; // ansi: registered through RegisterClass(Ex)A
struct Window {Class* cls;Proc proc;DWORD thread;bool alive=true,destroying=false,messageOnly=false;LONG_PTR user=0,id=0;HINSTANCE instance;HWND parent;LONG style,ex;int x,y,cx,cy;bool needSize=false;std::wstring title;std::vector<BYTE> extra;std::map<std::wstring,HANDLE> props;};
static Window* desktop;
static HWND focusWindow,activeWindow,foregroundWindow,captureWindow;
static void activateFirst(HWND h,Window* w);
static std::recursive_mutex mutex;
static std::vector<std::unique_ptr<Class>> classes;
static std::vector<std::unique_ptr<Window>> windows;
struct Atom {std::wstring name;unsigned refs;};
static std::map<ATOM,Atom> atoms;
static unsigned nextAtom=0xc000;
static bool integer(const void* p){return ULONG_PTR(p)<=0xffff;}
static std::wstring fold(std::wstring s){for(auto& c:s)if(c>=L'A'&&c<=L'Z')c+=32;return s;}
static Window* window(HWND h){for(auto& w:windows)if((HWND)w.get()==h&&w->alive)return w.get();SetLastError(ERROR_INVALID_WINDOW_HANDLE);return nullptr;}
// Wine win32u find_class: a NULL instance matches any class (SDL2 registers
// with GetModuleHandle(NULL) and creates with NULL), a global class (CS_GLOBALCLASS)
// matches every instance, and other instances compare at 64 KiB granularity.
static bool sameInstance(HINSTANCE registered,HINSTANCE requested){auto a=ULONG_PTR(registered),b=ULONG_PTR(requested);return a==b||((a>>16)&&(a&~ULONG_PTR(0xffff))==(b&~ULONG_PTR(0xffff)));}
static Class* klass(LPCWSTR name,HINSTANCE instance){for(auto& c:classes)if((integer(name)?c->atom==LOWORD(ULONG_PTR(name)):fold(c->name)==fold(name))&&(!instance||(c->info.style&0x4000)||sameInstance(c->info.instance,instance)))return c.get();return nullptr;}
static std::wstring key(LPCWSTR p){if(!integer(p))return L"s:"+fold(p);auto a=atoms.find(LOWORD(ULONG_PTR(p)));if(a!=atoms.end())return L"s:"+fold(a->second.name);return L"a:"+std::to_wstring(ULONG_PTR(p));}
static std::wstring wide(LPCSTR p){if(integer(p))return {};int n=MultiByteToWideChar(CP_ACP,0,p,-1,nullptr,0);std::wstring s(n? n:1,0);if(n)MultiByteToWideChar(CP_ACP,0,p,-1,&s[0],n);s.resize(n?n-1:0);return s;}
extern "C" ATOM WINAPI c_GlobalAddAtomW(LPCWSTR);
extern "C" ATOM WINAPI c_GlobalDeleteAtom(ATOM);
extern "C" ATOM WINAPI c_RegisterClassExW(const ClassEx* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p||p->size!=sizeof(*p)||!p->proc||integer(p->name)||!p->name[0]||p->clsExtra<0||p->wndExtra<0||p->clsExtra>4096||p->wndExtra>4096){SetLastError(ERROR_INVALID_PARAMETER);return 0;}if(klass(p->name,p->instance)){SetLastError(ERROR_CLASS_ALREADY_EXISTS);return 0;}if(classes.size()>=0x4000){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}auto c=std::make_unique<Class>();c->info=*p;c->name=p->name;c->atom=c_GlobalAddAtomW(p->name);if(!c->atom)return 0;if(!integer(p->menu)){c->menu=p->menu;c->info.menu=c->menu.c_str();}c->extra.resize(p->clsExtra);ATOM a=c->atom;classes.push_back(std::move(c));return a;}
extern "C" ATOM WINAPI c_RegisterClassW(const ClassW* p){if(!p){SetLastError(ERROR_INVALID_PARAMETER);return 0;}ClassEx x{sizeof(x),p->style,p->proc,p->clsExtra,p->wndExtra,p->instance,p->icon,p->cursor,p->brush,p->menu,p->name,nullptr};return c_RegisterClassExW(&x);}
extern "C" BOOL WINAPI c_UnregisterClassW(LPCWSTR n,HINSTANCE i){std::lock_guard<std::recursive_mutex> l(mutex);auto c=klass(n,i);if(!c){SetLastError(ERROR_CLASS_DOES_NOT_EXIST);return FALSE;}for(auto& w:windows)if(w->alive&&w->cls==c){SetLastError(ERROR_CLASS_HAS_WINDOWS);return FALSE;}c_GlobalDeleteAtom(c->atom);classes.erase(std::remove_if(classes.begin(),classes.end(),[&](auto& p){return p.get()==c;}),classes.end());return TRUE;}
extern "C" BOOL WINAPI c_GetClassInfoExW(HINSTANCE i,LPCWSTR n,ClassEx* p){std::lock_guard<std::recursive_mutex> l(mutex);auto c=klass(n,i);if(!c||!p){SetLastError(ERROR_CLASS_DOES_NOT_EXIST);return FALSE;}*p=c->info;p->name=c->name.c_str();return TRUE;}
extern "C" BOOL WINAPI c_DestroyWindow(HWND);
extern "C" LRESULT WINAPI c_DefWindowProcW(HWND h,UINT m,WPARAM w,LPARAM p){std::lock_guard<std::recursive_mutex> l(mutex);auto v=window(h);if(!v)return 0;switch(m){case WM_NCCREATE:return TRUE;case WM_CLOSE:c_DestroyWindow(h);return 0;case WM_SETTEXT:v->title=p?(LPCWSTR)p:L"";return TRUE;case WM_GETTEXTLENGTH:return v->title.size();case WM_GETTEXT:if(!p||!w)return 0;{size_t n=std::min(size_t(w-1),v->title.size());memcpy((void*)p,v->title.data(),n*2);((WCHAR*)p)[n]=0;return n;}default:return 0;}}
static void sendInitialSizeMove(HWND h,Window* w);static bool isFrameless(const Window* w);
extern "C" HWND WINAPI c_CreateWindowExW(DWORD ex,LPCWSTR n,LPCWSTR title,DWORD style,int x,int y,int cx,int cy,HWND parent,HMENU menu,HINSTANCE i,void* param){std::lock_guard<std::recursive_mutex> l(mutex);auto c=klass(n,i);if(!c){SetLastError(ERROR_CANNOT_FIND_WND_CLASS);return nullptr;}bool messageOnly=parent==HWND_MESSAGE;if(messageOnly)parent=nullptr;if(parent&&!window(parent))return nullptr;if(windows.size()>=4096){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}auto v=std::make_unique<Window>();v->cls=c;v->proc=c->info.proc;v->thread=GetCurrentThreadId();v->instance=i;v->parent=parent;v->messageOnly=messageOnly;v->id=(LONG_PTR)menu;v->style=style;v->ex=ex;v->x=x==CW_USEDEFAULT?0:x;v->y=y==CW_USEDEFAULT?0:y;v->cx=cx==CW_USEDEFAULT?640:std::max(0,cx);v->cy=cy==CW_USEDEFAULT?480:std::max(0,cy);v->title=title?title:L"";v->extra.resize(c->info.wndExtra);auto p=v.get();HWND h=(HWND)p;windows.push_back(std::move(v));Create cs{param,i,menu,parent,cy,cx,y,x,(LONG)style,title,n,ex};if(!p->proc(h,WM_NCCREATE,0,(LPARAM)&cs)){if(p->alive)p->proc(h,WM_NCDESTROY,0,0);p->alive=false;p->props.clear();return nullptr;}if(!p->alive)return nullptr;if(p->proc(h,WM_CREATE,0,(LPARAM)&cs)==-1){c_DestroyWindow(h);return nullptr;}if(p->alive){p->needSize=true;if((p->style&WS_VISIBLE)||isFrameless(p))sendInitialSizeMove(h,p);if(p->alive)activateFirst(h,p);}return p->alive?h:nullptr;}
extern "C" BOOL WINAPI c_DestroyWindow(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||w->destroying||w==desktop)return FALSE;if(w->thread!=GetCurrentThreadId()){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}w->destroying=true;w->proc(h,WM_DESTROY,0,0);std::vector<HWND> children;for(auto& child:windows)if(child->alive&&child->parent==h&&(child->style&WS_CHILD))children.push_back((HWND)child.get());for(auto child:children)c_DestroyWindow(child);w->proc(h,WM_NCDESTROY,0,0);w->alive=false;w->props.clear();return TRUE;}
static LONG_PTR value(Window* w,int index,size_t size,bool set,LONG_PTR v){LONG_PTR old=0;if(size==sizeof(LONG)&&(index==GWLP_WNDPROC||index==GWLP_HINSTANCE||index==GWLP_HWNDPARENT)){SetLastError(ERROR_INVALID_INDEX);return 0;}if(index>=0){if(size_t(index)>w->extra.size()||size>w->extra.size()-index){SetLastError(ERROR_INVALID_INDEX);return 0;}memcpy(&old,w->extra.data()+index,size);if(set)memcpy(w->extra.data()+index,&v,size);return old;}switch(index){case GWLP_USERDATA:old=w->user;if(set)w->user=v;break;case GWLP_WNDPROC:old=(LONG_PTR)w->proc;if(set)w->proc=(Proc)v;break;case GWLP_HINSTANCE:old=(LONG_PTR)w->instance;if(set)w->instance=(HINSTANCE)v;break;case GWLP_ID:old=w->id;if(set)w->id=v;break;case GWLP_HWNDPARENT:old=(LONG_PTR)w->parent;if(set){if(v&&!window((HWND)v))return 0;w->parent=(HWND)v;}break;case GWL_STYLE:old=w->style;if(set)w->style=(LONG)v;break;case GWL_EXSTYLE:old=w->ex;if(set)w->ex=(LONG)v;break;default:SetLastError(ERROR_INVALID_INDEX);}return old;}
extern "C" LONG_PTR WINAPI c_GetWindowLongPtrW(HWND h,int n){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w?value(w,n,sizeof(LONG_PTR),false,0):0;}
extern "C" LONG_PTR WINAPI c_SetWindowLongPtrW(HWND h,int n,LONG_PTR v){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w?value(w,n,sizeof(LONG_PTR),true,v):0;}
extern "C" LONG WINAPI c_GetWindowLongW(HWND h,int n){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w?(LONG)value(w,n,sizeof(LONG),false,0):0;}
extern "C" LONG WINAPI c_SetWindowLongW(HWND h,int n,LONG v){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w?(LONG)value(w,n,sizeof(LONG),true,v):0;}
extern "C" BOOL WINAPI c_IsWindow(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);return window(h)!=nullptr;}
extern "C" HWND WINAPI c_GetParent(HWND h){return (HWND)c_GetWindowLongPtrW(h,GWLP_HWNDPARENT);}
extern "C" BOOL WINAPI c_IsIconic(HWND h){return (c_GetWindowLongW(h,GWL_STYLE)&WS_MINIMIZE)!=0;}
extern "C" BOOL WINAPI c_IsWindowVisible(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);for(unsigned depth=0;h&&depth<4096;++depth){auto w=window(h);if(!w||!(w->style&WS_VISIBLE))return FALSE;if(!(w->style&WS_CHILD))return TRUE;h=w->parent;}return FALSE;}
extern "C" BOOL WINAPI c_ShowWindow(HWND h,int cmd){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;BOOL old=(w->style&WS_VISIBLE)!=0;if(cmd==SW_HIDE)w->style&=~WS_VISIBLE;else w->style|=WS_VISIBLE;if(cmd==SW_MINIMIZE||cmd==SW_SHOWMINIMIZED)w->style|=WS_MINIMIZE;if(cmd==SW_RESTORE)w->style&=~(WS_MINIMIZE|WS_MAXIMIZE);if(cmd!=SW_HIDE&&w->needSize&&w->alive)sendInitialSizeMove(h,w);if(cmd!=SW_HIDE&&cmd!=SW_MINIMIZE&&cmd!=SW_SHOWMINIMIZED&&cmd!=SW_SHOWNOACTIVATE&&cmd!=SW_SHOWMINNOACTIVE&&cmd!=SW_SHOWNA)activateFirst(h,w);return old;}
// The nonclient frame a window's style implies, exactly what AdjustWindowRectEx
// adds (a top-level window with an HMENU id has a menu bar): the client area is
// the window rect minus it, as on Windows. Games size their window as
// "client + frame" and expect GetClientRect/WM_SIZE to give the client back.
extern "C" BOOL WINAPI c_AdjustWindowRectEx(RECT* r,DWORD style,BOOL menu,DWORD ex);
static RECT frame(const Window* w){RECT r{};c_AdjustWindowRectEx(&r,w->style,!(w->style&WS_CHILD)&&w->id,w->ex);return {-r.left,-r.top,r.right,r.bottom};}
static LONG clientWidth(const Window* w){RECT f=frame(w);return std::max<LONG>(0,w->cx-f.left-f.right);}
static LONG clientHeight(const Window* w){RECT f=frame(w);return std::max<LONG>(0,w->cy-f.top-f.bottom);}
// Wine's model of the first size messages (what Windows games are tested
// against): a visible window, or one with no frame, gets WM_SIZE then WM_MOVE
// (client size, client origin) before CreateWindowEx returns; any other window
// gets them on its first show. Engines such as Halo Wars: DE learn their window
// size only from that first WM_SIZE and later divide by it.
static bool isFrameless(const Window* w){RECT f=frame(w);return !f.left&&!f.top&&!f.right&&!f.bottom;}
static void sendInitialSizeMove(HWND h,Window* w){
 if(!w->needSize)return;
 w->needSize=false;
 RECT f=frame(w);
 WPARAM kind=(w->style&WS_MINIMIZE)?SIZE_MINIMIZED:(w->style&WS_MAXIMIZE)?SIZE_MAXIMIZED:SIZE_RESTORED;
 w->proc(h,WM_SIZE,kind,MAKELPARAM(clientWidth(w),clientHeight(w)));
 if(w->alive)w->proc(h,WM_MOVE,0,MAKELPARAM((short)(w->x+f.left),(short)(w->y+f.top)));
}
// Windows activates the first top-level window an app shows: it becomes the
// foreground, active and focus window, with WM_ACTIVATEAPP, WM_ACTIVATE and
// WM_SETFOCUS. Unity's legacy input ignores pads while its window is not the
// foreground window (Slime Rancher), and nothing else here ever activates one.
static void activateFirst(HWND h,Window* w){
 if(!w->alive||w->messageOnly||(w->style&(WS_CHILD|WS_MINIMIZE))||!(w->style&WS_VISIBLE)||(w->ex&(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW)))return;
 if(foregroundWindow&&window(foregroundWindow))return;
 foregroundWindow=activeWindow=focusWindow=h;
 w->proc(h,WM_ACTIVATEAPP,TRUE,GetCurrentThreadId());
 if(w->alive)w->proc(h,WM_ACTIVATE,WA_ACTIVE,0);
 if(w->alive)w->proc(h,WM_SETFOCUS,0,0);
}
extern "C" BOOL WINAPI c_GetClientRect(HWND h,RECT* r){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!r)return FALSE;*r={0,0,clientWidth(w),clientHeight(w)};return TRUE;}
// Window origin in screen coordinates: a child's position is relative to its
// parent's client area. `client` adds this window's own frame.
static bool origin(HWND h,LONG& x,LONG& y,bool client=false,unsigned depth=0){
 auto w=window(h);if(!w||depth>=4096)return false;
 if((w->style&WS_CHILD)&&w->parent&&!origin(w->parent,x,y,true,depth+1))return false;
 x+=w->x;y+=w->y;
 if(client){RECT f=frame(w);x+=f.left;y+=f.top;}
 return true;
}
extern "C" BOOL WINAPI c_ClientToScreen(HWND h,POINT* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p)return FALSE;LONG x=0,y=0;if(!origin(h,x,y,true))return FALSE;p->x+=x;p->y+=y;return TRUE;}
extern "C" BOOL WINAPI c_ScreenToClient(HWND h,POINT* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p)return FALSE;LONG x=0,y=0;if(!origin(h,x,y,true))return FALSE;p->x-=x;p->y-=y;return TRUE;}
// Points from one window's client area to another's (NULL: the screen). The
// return value packs the x and y offsets added, as on Windows.
extern "C" int WINAPI c_MapWindowPoints(HWND from,HWND to,POINT* p,UINT n){
 std::lock_guard<std::recursive_mutex> l(mutex);LONG fx=0,fy=0,tx=0,ty=0;
 if((from&&!origin(from,fx,fy,true))||(to&&!origin(to,tx,ty,true))){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return 0;}
 const LONG dx=fx-tx,dy=fy-ty;if(p)for(UINT i=0;i<n;++i){p[i].x+=dx;p[i].y+=dy;}
 SetLastError(ERROR_SUCCESS);return MAKELONG(LOWORD(dx),LOWORD(dy));
}
extern "C" BOOL WINAPI c_GetWindowRect(HWND h,RECT* r){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!r)return FALSE;LONG x=0,y=0;if(!origin(h,x,y))return FALSE;*r={x,y,x+w->cx,y+w->cy};return TRUE;}
static LRESULT sendRaw(HWND h,UINT m,WPARAM w,LPARAM p){std::lock_guard<std::recursive_mutex> l(mutex);auto v=window(h);if(!v)return 0;if(v->thread!=GetCurrentThreadId()){SetLastError(ERROR_NOT_SUPPORTED);return 0;}return v->proc(h,m,w,p);}
static bool ansiWindow(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto v=window(h);return v&&v->cls&&v->cls->ansi;}
// An ANSI window procedure (RegisterClass(Ex)A) receives window text as ANSI:
// Unicode WM_SETTEXT/WM_GETTEXT payloads are converted for it, as Windows does.
extern "C" LRESULT WINAPI c_SendMessageW(HWND h,UINT m,WPARAM w,LPARAM p){
 if(!p||(m!=WM_SETTEXT&&m!=WM_GETTEXT)||!ansiWindow(h))return sendRaw(h,m,w,p);
 if(m==WM_SETTEXT){int n=WideCharToMultiByte(CP_ACP,0,(LPCWSTR)p,-1,nullptr,0,nullptr,nullptr);std::string text(n>0?size_t(n):1,'\0');if(n>0)WideCharToMultiByte(CP_ACP,0,(LPCWSTR)p,-1,&text[0],n,nullptr,nullptr);return sendRaw(h,m,w,(LPARAM)text.c_str());}
 if(!w)return 0;
 std::string buffer(size_t(w)*2,'\0');LRESULT bytes=sendRaw(h,m,WPARAM(buffer.size()),(LPARAM)&buffer[0]);
 int n=bytes>0?MultiByteToWideChar(CP_ACP,0,buffer.c_str(),int(bytes),nullptr,0):0;
 std::wstring text(size_t(n>0?n:0),L'\0');if(n>0)MultiByteToWideChar(CP_ACP,0,buffer.c_str(),int(bytes),&text[0],n);
 size_t chars=std::min(text.size(),size_t(w)-1);memcpy((void*)p,text.data(),chars*sizeof(WCHAR));((LPWSTR)p)[chars]=0;return LRESULT(chars);
}
extern "C" LRESULT WINAPI c_CallWindowProcW(Proc proc,HWND h,UINT m,WPARAM w,LPARAM p){return proc?proc(h,m,w,p):0;}
extern "C" BOOL WINAPI c_SetPropW(HWND h,LPCWSTR n,HANDLE v){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!n)return FALSE;w->props[key(n)]=v;return TRUE;}
extern "C" HANDLE WINAPI c_GetPropW(HWND h,LPCWSTR n){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!n)return nullptr;auto p=w->props.find(key(n));return p==w->props.end()?nullptr:p->second;}
extern "C" HANDLE WINAPI c_RemovePropW(HWND h,LPCWSTR n){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!n)return nullptr;auto p=w->props.find(key(n));if(p==w->props.end())return nullptr;HANDLE v=p->second;w->props.erase(p);return v;}
extern "C" BOOL WINAPI c_SetPropA(HWND h,LPCSTR n,HANDLE v){auto s=wide(n);return c_SetPropW(h,integer(n)?(LPCWSTR)n:s.c_str(),v);}
extern "C" HANDLE WINAPI c_GetPropA(HWND h,LPCSTR n){auto s=wide(n);return c_GetPropW(h,integer(n)?(LPCWSTR)n:s.c_str());}
extern "C" HANDLE WINAPI c_RemovePropA(HWND h,LPCSTR n){auto s=wide(n);return c_RemovePropW(h,integer(n)?(LPCWSTR)n:s.c_str());}
extern "C" ATOM WINAPI c_GlobalAddAtomW(LPCWSTR n){std::lock_guard<std::recursive_mutex> l(mutex);if(integer(n))return LOWORD(ULONG_PTR(n));if(!*n||wcslen(n)>255){SetLastError(ERROR_INVALID_PARAMETER);return 0;}for(auto& a:atoms)if(fold(a.second.name)==fold(n)){++a.second.refs;return a.first;}if(nextAtom>0xffff){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}ATOM a=(ATOM)nextAtom++;atoms.emplace(a,Atom{n,1});return a;}
extern "C" ATOM WINAPI c_GlobalFindAtomW(LPCWSTR n){std::lock_guard<std::recursive_mutex> l(mutex);if(integer(n))return LOWORD(ULONG_PTR(n));for(auto& a:atoms)if(fold(a.second.name)==fold(n))return a.first;SetLastError(ERROR_FILE_NOT_FOUND);return 0;}
extern "C" ATOM WINAPI c_GlobalDeleteAtom(ATOM a){std::lock_guard<std::recursive_mutex> l(mutex);if(a<0xc000)return 0;auto p=atoms.find(a);if(p==atoms.end()){SetLastError(ERROR_INVALID_HANDLE);return a;}if(!--p->second.refs)atoms.erase(p);return 0;}
// Rectangle functions use Wine's half-open geometry (right/bottom excluded).
extern "C" BOOL WINAPI c_CopyRect(RECT* d,const RECT* s){if(!d||!s)return FALSE;*d=*s;return TRUE;}
extern "C" BOOL WINAPI c_OffsetRect(RECT* r,int x,int y){if(!r)return FALSE;r->left+=x;r->right+=x;r->top+=y;r->bottom+=y;return TRUE;}
extern "C" BOOL WINAPI c_PtInRect(const RECT* r,POINT p){return r&&p.x>=r->left&&p.x<r->right&&p.y>=r->top&&p.y<r->bottom;}
extern "C" BOOL WINAPI c_SetRect(RECT* r,int l,int t,int rr,int b){if(!r)return FALSE;*r={l,t,rr,b};return TRUE;}
extern "C" BOOL WINAPI c_IsRectEmpty(const RECT* r){return !r||r->left>=r->right||r->top>=r->bottom;}
extern "C" BOOL WINAPI c_AllowSetForegroundWindow(DWORD){return TRUE;} // Matches Wine's current no-policy implementation.
using EnumProc=BOOL(CALLBACK*)(HWND,LPARAM);
extern "C" BOOL WINAPI c_EnumWindows(EnumProc proc,LPARAM p){if(!proc){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}std::vector<HWND> snapshot;{std::lock_guard<std::recursive_mutex> l(mutex);for(auto& w:windows)if(w->alive&&w.get()!=desktop&&!w->messageOnly&&!(w->style&WS_CHILD))snapshot.push_back((HWND)w.get());}for(auto h:snapshot)if(c_IsWindow(h)&&!proc(h,p))return FALSE;return TRUE;}
extern "C" HWND WINAPI c_GetDlgItem(HWND h,int id){std::lock_guard<std::recursive_mutex> l(mutex);if(!window(h))return nullptr;for(auto& w:windows)if(w->alive&&w->parent==h&&w->id==id)return (HWND)w.get();return nullptr;}
extern "C" LRESULT WINAPI c_SendDlgItemMessageW(HWND h,int id,UINT m,WPARAM w,LPARAM p){return c_SendMessageW(c_GetDlgItem(h,id),m,w,p);}
extern "C" BOOL WINAPI c_SetWindowTextW(HWND h,LPCWSTR s){return (BOOL)c_SendMessageW(h,WM_SETTEXT,0,(LPARAM)s);}
extern "C" HWND WINAPI c_GetDesktopWindow();
// GetAncestor: GA_PARENT is the parent of a child window, the desktop for a
// top-level one; GA_ROOT climbs child links; GA_ROOTOWNER also follows owners.
extern "C" HWND WINAPI c_GetAncestor(HWND h,UINT flags){std::lock_guard<std::recursive_mutex> l(mutex);Window* w=window(h);if(!w)return nullptr;if(w==desktop)return nullptr;
 auto up=[&](Window* x)->Window*{if(!(x->style&WS_CHILD)||!x->parent)return nullptr;Window* p=window(x->parent);return p==desktop?nullptr:p;};
 auto root=[&](Window* x){while(Window* p=up(x))x=p;return x;};
 if(flags==GA_PARENT){Window* p=up(w);return p?(HWND)p:c_GetDesktopWindow();}
 Window* r=root(w);if(flags==GA_ROOT)return (HWND)r;
 if(flags==GA_ROOTOWNER){for(int guard=0;guard<64&&r->parent;++guard){Window* o=window(r->parent);if(!o||o==desktop)break;r=root(o);}return (HWND)r;}
 SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
// No dialog manager: dialog templates are not instantiated. Games import these
// for optional setup/crash dialogs; creating one fails cleanly, and with no
// dialogs alive no message is a dialog message.
extern "C" HWND WINAPI c_CreateDialogParamW(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
extern "C" HWND WINAPI c_CreateDialogParamA(HINSTANCE,LPCSTR,HWND,DLGPROC,LPARAM){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
extern "C" BOOL WINAPI c_IsDialogMessageW(HWND,LPMSG){return FALSE;}
extern "C" BOOL WINAPI c_IsDialogMessageA(HWND,LPMSG){return FALSE;}
extern "C" BOOL WINAPI c_CheckDlgButton(HWND h,int id,UINT check){HWND item=c_GetDlgItem(h,id);if(!item)return FALSE;c_SendMessageW(item,BM_SETCHECK,check,0);return TRUE;}
extern "C" UINT WINAPI c_IsDlgButtonChecked(HWND h,int id){HWND item=c_GetDlgItem(h,id);return item?(UINT)c_SendMessageW(item,BM_GETCHECK,0,0):0;}
extern "C" BOOL WINAPI c_SetDlgItemTextW(HWND h,int id,LPCWSTR s){return c_SetWindowTextW(c_GetDlgItem(h,id),s);}
extern "C" BOOL WINAPI c_MoveWindow(HWND h,int x,int y,int cx,int cy,BOOL){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;w->x=x;w->y=y;w->cx=std::max(0,cx);w->cy=std::max(0,cy);w->proc(h,WM_MOVE,0,MAKELPARAM(x,y));if(w->alive)w->proc(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(clientWidth(w),clientHeight(w)));return TRUE;}
extern "C" BOOL WINAPI c_InflateRect(RECT* r,int x,int y){if(!r)return FALSE;r->left-=x;r->top-=y;r->right+=x;r->bottom+=y;return TRUE;}
extern "C" BOOL WINAPI c_UnionRect(RECT* d,const RECT* a,const RECT* b){if(!d||!a||!b)return FALSE;RECT x=*a,y=*b;if(c_IsRectEmpty(&x)){*d=c_IsRectEmpty(&y)?RECT{}:y;}else if(c_IsRectEmpty(&y))*d=x;else *d={std::min(x.left,y.left),std::min(x.top,y.top),std::max(x.right,y.right),std::max(x.bottom,y.bottom)};return !c_IsRectEmpty(d);}
// One virtual desktop/monitor record. Dimensions are supplied by the launcher;
// these APIs describe records only and never create or query a native window.
static LONG desktopWidth=1920,desktopHeight=1080;

static int monitorToken;
struct MonitorInfo {DWORD size;RECT monitor,work;DWORD flags;};
struct MonitorInfoW {MonitorInfo base;WCHAR device[32];};
struct MonitorInfoA {MonitorInfo base;CHAR device[32];};
extern "C" void WINAPI uwph_u32c_configure(LONG x,LONG y){std::lock_guard<std::recursive_mutex> l(mutex);if(x>0&&y>0){desktopWidth=x;desktopHeight=y;if(desktop){desktop->cx=x;desktop->cy=y;}}}
extern "C" HWND WINAPI c_GetDesktopWindow(){std::lock_guard<std::recursive_mutex> l(mutex);if(!desktop){auto w=std::make_unique<Window>();w->cls=nullptr;w->proc=c_DefWindowProcW;w->thread=GetCurrentThreadId();w->parent=nullptr;w->instance=nullptr;w->style=WS_VISIBLE;w->ex=0;w->x=w->y=0;w->cx=desktopWidth;w->cy=desktopHeight;desktop=w.get();windows.push_back(std::move(w));}return (HWND)desktop;}
extern "C" HMONITOR WINAPI c_MonitorFromRect(const RECT* r,DWORD flags){std::lock_guard<std::recursive_mutex> l(mutex);if(!r)return nullptr;bool hit=r->left<desktopWidth&&r->right>0&&r->top<desktopHeight&&r->bottom>0;return hit||(flags&3)?(HMONITOR)&monitorToken:nullptr;}
extern "C" HMONITOR WINAPI c_MonitorFromWindow(HWND h,DWORD flags){RECT r;if(c_GetWindowRect(h,&r))return c_MonitorFromRect(&r,flags);return flags&3?(HMONITOR)&monitorToken:nullptr;}
extern "C" BOOL WINAPI c_GetMonitorInfoW(HMONITOR h,MonitorInfo* p){std::lock_guard<std::recursive_mutex> l(mutex);if(h!=(HMONITOR)&monitorToken||!p||(p->size!=sizeof(MonitorInfo)&&p->size!=sizeof(MonitorInfoW))){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}p->monitor=p->work=RECT{0,0,desktopWidth,desktopHeight};p->flags=1;if(p->size==sizeof(MonitorInfoW))wcscpy(((MonitorInfoW*)p)->device,L"\\\\.\\DISPLAY1");return TRUE;}
extern "C" BOOL WINAPI c_GetMonitorInfoA(HMONITOR h,MonitorInfo* p){if(!p||(p->size!=sizeof(MonitorInfo)&&p->size!=sizeof(MonitorInfoA))){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}MonitorInfo w{sizeof(w)};if(!c_GetMonitorInfoW(h,&w))return FALSE;p->monitor=w.monitor;p->work=w.work;p->flags=w.flags;if(p->size==sizeof(MonitorInfoA))strcpy(((MonitorInfoA*)p)->device,"\\\\.\\DISPLAY1");return TRUE;}
#include "system_metrics.h"
extern "C" int WINAPI c_GetSystemMetrics(int n){std::lock_guard<std::recursive_mutex> l(mutex);return classic_system_metric(n,desktopWidth,desktopHeight);}

// ---- item 44: input/cursor/focus. No CoreWindow/HID backend is wired from this
// record-only DLL; state defaults to "nothing pressed/no capture/no focus",
// which is the Windows-correct answer until a real input feed is attached.
// uwph_u32c_key_event is this module's own hook for that future wiring, not a
// Windows export.
static BYTE keyDown[256];      // bit0x80 set while key reported down
static BYTE keyPressedSince[256]; // bit0x01: pressed since last GetAsyncKeyState poll
static bool haveCursorPos=false; static POINT cursorPos2; static bool hasClip=false; static RECT clipRectV;
static int cursorShowCount=0; static HCURSOR currentCursor;
static std::vector<RawInputDevice2> rawDevices;
static POINT& cursorPosRef(){if(!haveCursorPos){cursorPos2={desktopWidth/2,desktopHeight/2};haveCursorPos=true;}return cursorPos2;}
#include "lock_keys.h"
extern "C" void WINAPI uwph_u32c_key_event(int vk,BOOL down){std::lock_guard<std::recursive_mutex> l(mutex);if(vk<0||vk>255)return;if(down){u32c_input::toggle_lock(keyDown[vk],vk);keyDown[vk]|=0x80;keyPressedSince[vk]|=0x01;}else keyDown[vk]&=~0x80;}
extern "C" SHORT WINAPI c_GetAsyncKeyState(int vk){std::lock_guard<std::recursive_mutex> l(mutex);if(vk<0||vk>255)return 0;SHORT r=(keyDown[vk]&0x80)?(SHORT)0x8000:0;if(keyPressedSince[vk]&0x01){r|=1;keyPressedSince[vk]&=~0x01;}return r;}
extern "C" BOOL WINAPI c_GetKeyboardState(PBYTE state){if(!state){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}std::lock_guard<std::recursive_mutex> l(mutex);memcpy(state,keyDown,256);return TRUE;}
extern "C" SHORT WINAPI c_GetKeyState(int vk){std::lock_guard<std::recursive_mutex> l(mutex);if(vk<0||vk>255)return 0;return SHORT(((keyDown[vk]&0x80)?0x8000:0)|(keyDown[vk]&1));}
extern "C" HWND WINAPI c_GetFocus(){std::lock_guard<std::recursive_mutex> l(mutex);return focusWindow&&window(focusWindow)?focusWindow:nullptr;}
extern "C" HWND WINAPI c_SetFocus(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);HWND old=c_GetFocus();if(!h){focusWindow=nullptr;return old;}auto w=window(h);if(!w)return nullptr;if(w->thread!=GetCurrentThreadId()){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}focusWindow=h;return old;}
extern "C" HWND WINAPI c_GetActiveWindow(){std::lock_guard<std::recursive_mutex> l(mutex);return activeWindow&&window(activeWindow)?activeWindow:nullptr;}
extern "C" HWND WINAPI c_GetForegroundWindow(){std::lock_guard<std::recursive_mutex> l(mutex);return foregroundWindow&&window(foregroundWindow)?foregroundWindow:nullptr;}
extern "C" BOOL WINAPI c_SetForegroundWindow(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;foregroundWindow=h;activeWindow=h;return TRUE;}
extern "C" HWND WINAPI c_SetCapture(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return nullptr;HWND old=captureWindow;captureWindow=h;return old;}
extern "C" BOOL WINAPI c_ReleaseCapture(){std::lock_guard<std::recursive_mutex> l(mutex);captureWindow=nullptr;return TRUE;}
static void clampToClip(POINT& p){if(hasClip){if(p.x<clipRectV.left)p.x=clipRectV.left;if(p.x>=clipRectV.right)p.x=clipRectV.right-1;if(p.y<clipRectV.top)p.y=clipRectV.top;if(p.y>=clipRectV.bottom)p.y=clipRectV.bottom-1;}}
extern "C" BOOL WINAPI c_ClipCursor(const RECT* r){std::lock_guard<std::recursive_mutex> l(mutex);if(r){if(r->left>r->right||r->top>r->bottom){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}clipRectV=*r;hasClip=true;clampToClip(cursorPosRef());}else hasClip=false;return TRUE;}
extern "C" BOOL WINAPI c_SetCursorPos(int x,int y){std::lock_guard<std::recursive_mutex> l(mutex);POINT p{x,y};clampToClip(p);cursorPosRef()=p;haveCursorPos=true;return TRUE;}
extern "C" BOOL WINAPI c_GetCursorPos(POINT* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p)return FALSE;*p=cursorPosRef();return TRUE;}
extern "C" HCURSOR WINAPI c_SetCursor(HCURSOR c){std::lock_guard<std::recursive_mutex> l(mutex);HCURSOR old=currentCursor;currentCursor=c;return old;}
extern "C" int WINAPI c_ShowCursor(BOOL show){std::lock_guard<std::recursive_mutex> l(mutex);cursorShowCount+=show?1:-1;return cursorShowCount;}
#include "display_config.h"
using MonitorEnumProc=BOOL(CALLBACK*)(HMONITOR,HDC,RECT*,LPARAM);
extern "C" BOOL WINAPI c_EnumDisplayMonitors(HDC hdc,const RECT* clip,MonitorEnumProc proc,LPARAM p){
 if(!proc){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 std::lock_guard<std::recursive_mutex> l(mutex);
 RECT r{0,0,desktopWidth,desktopHeight};
 if(clip){RECT i{std::max(r.left,clip->left),std::max(r.top,clip->top),std::min(r.right,clip->right),std::min(r.bottom,clip->bottom)};if(i.left>=i.right||i.top>=i.bottom)return TRUE;r=i;}
 return proc((HMONITOR)&monitorToken,hdc,&r,p);
}
static void fillMode(DWORD& fields,DWORD& w,DWORD& h,DWORD& bpp,DWORD& freq,POINTL& pos){fields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFREQUENCY|DM_POSITION;w=desktopWidth;h=desktopHeight;bpp=32;freq=60;pos={0,0};}
extern "C" BOOL WINAPI c_EnumDisplaySettingsW(LPCWSTR,DWORD mode,DEVMODEW* dm){
 if(!dm){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(mode!=0&&mode!=ENUM_CURRENT_SETTINGS&&mode!=ENUM_REGISTRY_SETTINGS)return FALSE; // one supported mode only
 std::lock_guard<std::recursive_mutex> l(mutex);
 fillMode(dm->dmFields,dm->dmPelsWidth,dm->dmPelsHeight,dm->dmBitsPerPel,dm->dmDisplayFrequency,dm->dmPosition);
 return TRUE;
}
extern "C" BOOL WINAPI c_EnumDisplaySettingsA(LPCSTR,DWORD mode,DEVMODEA* dm){
 if(!dm){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(mode!=0&&mode!=ENUM_CURRENT_SETTINGS&&mode!=ENUM_REGISTRY_SETTINGS)return FALSE;
 std::lock_guard<std::recursive_mutex> l(mutex);
 fillMode(dm->dmFields,dm->dmPelsWidth,dm->dmPelsHeight,dm->dmBitsPerPel,dm->dmDisplayFrequency,dm->dmPosition);
 return TRUE;
}
extern "C" BOOL WINAPI c_EnumDisplayDevicesA(LPCSTR device,DWORD num,DISPLAY_DEVICEA* dd,DWORD){
 if(!dd)return FALSE;
 if(!device){ if(num!=0)return FALSE;
  strcpy(dd->DeviceName,"\\\\.\\DISPLAY1");strcpy(dd->DeviceString,"U32C Record Display");
  dd->StateFlags=0x1/*DISPLAY_DEVICE_ATTACHED_TO_DESKTOP*/|0x4/*DISPLAY_DEVICE_PRIMARY_DEVICE*/;
  dd->DeviceID[0]=0;dd->DeviceKey[0]=0;return TRUE; }
 if(strcmp(device,"\\\\.\\DISPLAY1")||num!=0)return FALSE;
 strcpy(dd->DeviceName,"\\\\.\\DISPLAY1\\Monitor0");strcpy(dd->DeviceString,"U32C Record Monitor");
 dd->StateFlags=0x1;dd->DeviceID[0]=0;dd->DeviceKey[0]=0;return TRUE;
}
extern "C" BOOL WINAPI c_GetWindowPlacement(HWND h,WindowPlacement2* wp){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto w=window(h);if(!w)return FALSE;
 if(!wp||wp->length!=sizeof(WindowPlacement2)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 wp->flags=0;wp->showCmd=(w->style&WS_MINIMIZE)?SW_SHOWMINIMIZED:(w->style&WS_MAXIMIZE)?SW_SHOWMAXIMIZED:(w->style&WS_VISIBLE)?SW_SHOWNORMAL:SW_HIDE;
 wp->ptMinPosition={0,0};wp->ptMaxPosition={-1,-1};
 c_GetWindowRect(h,&wp->rcNormalPosition);
 return TRUE;
}
extern "C" BOOL WINAPI c_RegisterRawInputDevices(const RawInputDevice2* devices,UINT count,UINT size){
 if(!devices||size!=sizeof(RawInputDevice2)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 std::lock_guard<std::recursive_mutex> l(mutex);
 for(UINT i=0;i<count;++i){auto d=devices[i];
  if(d.dwFlags&RIDEV_REMOVE){rawDevices.erase(std::remove_if(rawDevices.begin(),rawDevices.end(),[&](auto& r){return r.usUsagePage==d.usUsagePage&&r.usUsage==d.usUsage;}),rawDevices.end());continue;}
  if(d.hwndTarget&&!window(d.hwndTarget))return FALSE;
  rawDevices.erase(std::remove_if(rawDevices.begin(),rawDevices.end(),[&](auto& r){return r.usUsagePage==d.usUsagePage&&r.usUsage==d.usUsage;}),rawDevices.end());
  rawDevices.push_back(d);
 }
 return TRUE;
}
// Wine 10 NtUserGetRegisteredRawInputDevices: sizes, NULL-buffer count query,
// ERROR_INSUFFICIENT_BUFFER with the required count, else a copy of the table.
extern "C" UINT WINAPI c_GetRegisteredRawInputDevices(RawInputDevice2* devices,UINT* count,UINT size){
 if(size!=sizeof(RawInputDevice2)||!count||(devices&&!*count)){SetLastError(ERROR_INVALID_PARAMETER);return ~0u;}
 std::lock_guard<std::recursive_mutex> l(mutex);
 size_t capacity=size_t(*count)*size;*count=UINT(rawDevices.size());
 size_t needed=size_t(size)*rawDevices.size();
 if(!devices)return 0;
 if(capacity<needed){SetLastError(ERROR_INSUFFICIENT_BUFFER);return ~0u;}
 if(needed)memcpy(devices,rawDevices.data(),needed);
 return *count;
}
// No HID backend feeds this record module, so the honest device list is empty
// and any raw-input handle is unknown; both match real Windows error paths.
extern "C" UINT WINAPI c_GetRawInputDeviceList(RawInputDeviceList2* list,UINT* count,UINT size){
 if(!count||size!=sizeof(RawInputDeviceList2)){SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;}
 if(!list){*count=0;return 0;}
 *count=0;return 0;
}
extern "C" UINT WINAPI c_GetRawInputDeviceInfoW(HANDLE,UINT,LPVOID,PUINT){SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;}
extern "C" UINT WINAPI c_GetRawInputData(HANDLE,UINT,LPVOID data,PUINT size,UINT headerSize){
 if(!size||headerSize!=sizeof(RawInputHeader2)){SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1;}
 if(!data){*size=0;return 0;}
 SetLastError(ERROR_INVALID_PARAMETER);return (UINT)-1; // handle is never one we issued
}
extern "C" INT WINAPI c_GetRawInputBuffer(LPVOID data,PUINT size,UINT headerSize){
 if(!size||headerSize!=sizeof(RawInputHeader2)){SetLastError(ERROR_INVALID_PARAMETER);return -1;}
 if(!data){*size=sizeof(RawInputHeader2)+32;return 0;}
 return 0; // no buffered raw input: nothing was ever produced
}
extern "C" HKL WINAPI c_GetKeyboardLayout(DWORD){return (HKL)(ULONG_PTR)0x04090409;} // fixed en-US layout
// A standard enhanced 101/102-key keyboard with 12 function keys (as Wine reports).
extern "C" int WINAPI c_GetKeyboardType(int type){return type==0?4:type==2?12:0;}
// String tables come straight from the module's resource directory, so this
// serves images Play mapped itself as well as system modules.
static const IMAGE_RESOURCE_DIRECTORY_ENTRY* resourceEntry(const BYTE* base,size_t size,DWORD rva,WORD id,bool any){
 if(rva>size||size-rva<sizeof(IMAGE_RESOURCE_DIRECTORY))return nullptr;
 auto dir=(const IMAGE_RESOURCE_DIRECTORY*)(base+rva);
 size_t n=size_t(dir->NumberOfNamedEntries)+dir->NumberOfIdEntries;
 if((size-rva-sizeof *dir)/sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY)<n)return nullptr;
 auto e=(const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(dir+1);
 for(size_t i=0;i<n;++i)if(!e[i].NameIsString&&(any||e[i].Id==id))return &e[i];
 return nullptr;
}
static bool stringResource(HINSTANCE h,UINT id,const WCHAR*& text,size_t& length){
 auto base=(const BYTE*)(h?h:GetModuleHandleW(nullptr));if(!base)return false;
 auto dos=(const IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x10000)return false;
 auto nt=(const IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
 const size_t size=nt->OptionalHeader.SizeOfImage;
 auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
 if(!d.VirtualAddress||d.VirtualAddress>=size)return false;
 const DWORD root=d.VirtualAddress;
 auto type=resourceEntry(base,size,root,6/*RT_STRING*/,false);if(!type||!type->DataIsDirectory)return false;
 auto block=resourceEntry(base,size,root+type->OffsetToDirectory,WORD((id>>4)+1),false);if(!block||!block->DataIsDirectory)return false;
 const DWORD languages=root+block->OffsetToDirectory;
 auto language=resourceEntry(base,size,languages,0,false);              // neutral
 if(!language)language=resourceEntry(base,size,languages,0x409,false);  // en-US
 if(!language)language=resourceEntry(base,size,languages,0,true);
 if(!language||language->DataIsDirectory)return false;
 const size_t at=size_t(root)+language->OffsetToData;
 if(at>size||size-at<sizeof(IMAGE_RESOURCE_DATA_ENTRY))return false;
 auto data=(const IMAGE_RESOURCE_DATA_ENTRY*)(base+at);
 if(data->OffsetToData>size||size-data->OffsetToData<data->Size)return false;
 auto p=(const WCHAR*)(base+data->OffsetToData),end=p+data->Size/sizeof(WCHAR);
 for(UINT i=0;i<(id&15);++i){if(p>=end||size_t(end-p-1)<*p)return false;p+=1+*p;}
 if(p>=end||size_t(end-p-1)<*p)return false;
 text=p+1;length=*p;return true;
}
// As Wine: a missing or empty string yields "" (and 0).
extern "C" int WINAPI c_LoadStringW(HINSTANCE h,UINT id,LPWSTR buffer,int max){
 if(!buffer||max<0){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 const WCHAR* text=nullptr;size_t length=0;
 if(!stringResource(h,LOWORD(id),text,length)){if(max>0)buffer[0]=0;SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}
 if(!max){*(const WCHAR**)buffer=text;return int(length);}   // read-only pointer into the string table
 if(!length){buffer[0]=0;return 0;}
 const size_t n=std::min(length,size_t(max-1));memcpy(buffer,text,n*sizeof(WCHAR));buffer[n]=0;return int(n);
}
extern "C" BOOL WINAPI c_GetKeyboardLayoutNameW(LPWSTR name){if(!name)return FALSE;wcscpy(name,L"00000409");return TRUE;}
#include "map_char.h"
static unsigned mapVk(UINT code,UINT type){
 switch(type){
 case MAPVK_VK_TO_VSC: case MAPVK_VK_TO_VSC_EX:
  for(auto& r:u32c_keymap::rows) if(r.vk==code||u32c_keymap::generic(r.vk)==code) return type==MAPVK_VK_TO_VSC_EX&&r.ext?(0xE000|r.scan):r.scan;
  return 0;
 case MAPVK_VSC_TO_VK: case MAPVK_VSC_TO_VK_EX: {
  bool ext=(code&0xff00)==0xE000; BYTE scan=(BYTE)(code&0xff);
  for(auto& r:u32c_keymap::rows) if(r.scan==scan&&r.ext==ext) return type==MAPVK_VSC_TO_VK_EX?r.vk:u32c_keymap::generic(r.vk);
  return 0; }
 case MAPVK_VK_TO_CHAR:
  if(UINT ch=u32c_map_char::extra_character(code))return ch;
  for(auto& r:u32c_keymap::rows) if(r.vk==code&&r.ch) return r.ch;
  return 0;
 default: return 0;
 }
}
extern "C" UINT WINAPI c_MapVirtualKeyW(UINT code,UINT type){std::lock_guard<std::recursive_mutex> l(mutex);return mapVk(code,type);}
extern "C" UINT WINAPI c_MapVirtualKeyExA(UINT code,UINT type,HKL){return c_MapVirtualKeyW(code,type);}
#include "unicode_letters.h"
#include "unicode_punctuation.h"
#include "unicode_control.h"
#include "unicode_special.h"
#include "unicode_keypad.h"
extern "C" int WINAPI c_ToUnicode(UINT vk,UINT,const BYTE* state,LPWSTR buf,int bufSize,UINT){
 if(!buf||bufSize<1)return 0;
 int keypad=u32c_unicode::keypad_operator(vk,state,buf,bufSize);if(keypad>=0)return keypad;
 int special=u32c_unicode::special_key(vk,state,buf,bufSize);if(special>=0)return special;
 int control=u32c_unicode::control_letter(vk,state,buf,bufSize);if(control>=0)return control;
 char ch=0;for(auto& r:u32c_keymap::rows)if(u32c_keymap::generic(r.vk)==vk&&r.ch){ch=r.ch;break;}
 if(!ch)return 0; // dead/non-printable key: no composition table, honest "no translation"
 ch=u32c_unicode::punctuation_case(u32c_unicode::letter_case(ch,state),state);
 buf[0]=(WCHAR)(unsigned char)ch;if(bufSize>1)buf[1]=0;return 1;
}

// ---- batch 2: clipboard, timers/tracking bookkeeping, dialog/message helpers,
// simple metrics and non-pump window geometry. No GDI/paint backend exists, so
// ValidateRect/UpdateWindow are honest no-ops (never any pending invalid region
// to clear/flush) and SetWindowPos tracks no z-order (same limitation as
// EnumWindows, documented in README.md). Timer/TrackMouseEvent entries are
// record-only: actual WM_TIMER/WM_MOUSELEAVE delivery needs the message pump
// (item 43/upmp), left to that lane on purpose.
static bool clipboardOpen=false; static HWND clipboardOwner; static DWORD clipboardSequence=0;
static std::map<UINT,HANDLE> clipboardData;
extern "C" BOOL WINAPI c_OpenClipboard(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);if(h&&!window(h))return FALSE;if(clipboardOpen){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}clipboardOpen=true;clipboardOwner=h;return TRUE;}
extern "C" BOOL WINAPI c_CloseClipboard(){std::lock_guard<std::recursive_mutex> l(mutex);if(!clipboardOpen)return FALSE;clipboardOpen=false;return TRUE;}
extern "C" BOOL WINAPI c_EmptyClipboard(){std::lock_guard<std::recursive_mutex> l(mutex);if(!clipboardOpen)return FALSE;clipboardData.clear();++clipboardSequence;return TRUE;}
extern "C" HANDLE WINAPI c_GetClipboardData(UINT fmt){std::lock_guard<std::recursive_mutex> l(mutex);if(!clipboardOpen)return nullptr;auto p=clipboardData.find(fmt);SetLastError(0);return p==clipboardData.end()?nullptr:p->second;}
extern "C" HANDLE WINAPI c_SetClipboardData(UINT fmt,HANDLE data){std::lock_guard<std::recursive_mutex> l(mutex);if(!clipboardOpen){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}clipboardData[fmt]=data;++clipboardSequence;return data;}
extern "C" BOOL WINAPI c_IsClipboardFormatAvailable(UINT fmt){std::lock_guard<std::recursive_mutex> l(mutex);return clipboardData.count(fmt)!=0;}

struct TimerEntry{HWND hwnd;UINT_PTR id;};
static std::vector<TimerEntry> timers; static UINT_PTR nextTimerId=1;
extern "C" UINT_PTR WINAPI c_SetTimer(HWND hwnd,UINT_PTR id,UINT elapse,void*){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(hwnd&&!window(hwnd))return 0;
 if(!hwnd||!id)id=nextTimerId++;
 (void)elapse; // bookkeeping only: WM_TIMER delivery needs the message pump (item 43)
 for(auto& t:timers)if(t.hwnd==hwnd&&t.id==id)return id;
 timers.push_back({hwnd,id});return id;
}
extern "C" BOOL WINAPI c_KillTimer(HWND hwnd,UINT_PTR id){std::lock_guard<std::recursive_mutex> l(mutex);auto n=timers.size();timers.erase(std::remove_if(timers.begin(),timers.end(),[&](auto& t){return t.hwnd==hwnd&&t.id==id;}),timers.end());return timers.size()!=n;}

#ifndef TME_HOVER
#define TME_HOVER 0x00000001
#define TME_LEAVE 0x00000002
#define TME_NONCLIENT 0x00000010
#define TME_CANCEL 0x80000000
#endif
struct TrackMouseEvent2{DWORD size,flags;HWND hwndTrack;DWORD hoverTime;};
static_assert(sizeof(TrackMouseEvent2)==24,"TRACKMOUSEEVENT layout");
static std::vector<TrackMouseEvent2> tracked;
extern "C" BOOL WINAPI c_TrackMouseEvent(TrackMouseEvent2* e){
 if(!e||e->size!=sizeof(TrackMouseEvent2)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(e->flags&TME_CANCEL){tracked.erase(std::remove_if(tracked.begin(),tracked.end(),[&](auto& t){return t.hwndTrack==e->hwndTrack;}),tracked.end());return TRUE;}
 if(!window(e->hwndTrack))return FALSE;
 tracked.erase(std::remove_if(tracked.begin(),tracked.end(),[&](auto& t){return t.hwndTrack==e->hwndTrack;}),tracked.end());
 tracked.push_back(*e);return TRUE; // WM_MOUSELEAVE/HOVER delivery needs the message pump (item 43)
}

extern "C" UINT WINAPI c_RegisterWindowMessageA(LPCSTR s){auto w=wide(s);return c_GlobalAddAtomW(integer(s)?(LPCWSTR)s:w.c_str());} // Wine routes this through the same atom table.
extern "C" LRESULT WINAPI c_SendDlgItemMessageA(HWND h,int id,UINT m,WPARAM w,LPARAM p){return c_SendMessageW(c_GetDlgItem(h,id),m,w,p);} // no ANSI<->Unicode text thunking (WM_SETTEXT/WM_GETTEXT): not claimed.
extern "C" BOOL WINAPI c_SendMessageTimeoutA(HWND h,UINT m,WPARAM w,LPARAM p,UINT,UINT,DWORD_PTR* result){std::lock_guard<std::recursive_mutex> l(mutex);auto v=window(h);if(!v)return FALSE;if(v->thread!=GetCurrentThreadId()){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}LRESULT r=v->proc(h,m,w,p);if(result)*result=(DWORD_PTR)r;return TRUE;}
extern "C" BOOL WINAPI c_SetDlgItemTextA(HWND h,int id,LPCSTR s){auto w=wide(s);return c_SetDlgItemTextW(h,id,integer(s)?(LPCWSTR)s:w.c_str());}

extern "C" DWORD WINAPI c_GetCaretBlinkTime(){return 530;} // Windows default.
extern "C" UINT WINAPI c_GetDoubleClickTime(){return 500;} // Windows default.
static int threadDesktopToken;
extern "C" HANDLE WINAPI c_GetThreadDesktop(DWORD){return (HANDLE)&threadDesktopToken;} // single-desktop-session model.
#ifndef UOI_FLAGS
#define UOI_FLAGS 1
#define UOI_NAME 2
#define UOI_TYPE 3
#endif
#ifndef WSF_VISIBLE
#define WSF_VISIBLE 1
#endif
#include "object_flags.h"
#include "object_strings.h"
// One interactive window station, WinSta0, holding the one desktop.
static int stationToken;
extern "C" HWINSTA WINAPI c_GetProcessWindowStation(){return (HWINSTA)&stationToken;}
static bool userObject(HANDLE h,bool& station){station=h==(HANDLE)&stationToken;return station||h==(HANDLE)&threadDesktopToken;}
extern "C" BOOL WINAPI c_GetUserObjectInformationA(HANDLE h,int index,void* info,DWORD len,DWORD* needed){
 bool station;if(!userObject(h,station)){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
 if(index==UOI_FLAGS)return object_flags(info,len,needed,station?WSF_VISIBLE:0);
 const char* s=index==UOI_NAME?(station?"WinSta0":"Default"):index==UOI_TYPE?(station?"WindowStation":"Desktop"):nullptr;
 if(!s){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 return object_string(s,info,len,needed);
}
extern "C" BOOL WINAPI c_GetUserObjectInformationW(HANDLE h,int index,void* info,DWORD len,DWORD* needed){
 bool station;if(!userObject(h,station)){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
 if(index==UOI_FLAGS)return object_flags(info,len,needed,station?WSF_VISIBLE:0);
 const wchar_t* s=index==UOI_NAME?(station?L"WinSta0":L"Default"):index==UOI_TYPE?(station?L"WindowStation":L"Desktop"):nullptr;
 if(!s){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 const DWORD bytes=DWORD((wcslen(s)+1)*sizeof(WCHAR));
 if(needed)*needed=bytes;
 if(len<bytes){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 if(!info){SetLastError(ERROR_NOACCESS);return FALSE;}
 memcpy(info,s,bytes);return TRUE;
}
#ifndef SPI_GETWORKAREA
#define SPI_GETWORKAREA 0x0030
#endif
#include "input_parameters.h"
extern "C" BOOL WINAPI c_SystemParametersInfoW(UINT action,UINT,void* pv,UINT){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(action==SPI_GETWORKAREA){if(!pv)return FALSE;*(RECT*)pv={0,0,desktopWidth,desktopHeight};return TRUE;} // no taskbar: work area equals the full desktop.
 UINT value;
 if(input_parameter_default(action,value)){if(!pv)return FALSE;*(UINT*)pv=value;return TRUE;}
 SetLastError(ERROR_NOT_SUPPORTED);return FALSE; // honest: no other SPI_* backend exists.
}

// Fixed classic (96 DPI, unthemed) window-geometry metrics (Wine/Windows
// border/caption/menu defaults); this record module has no real display
// driver to report DPI/theme-scaled values from.
#include "window_caption.h"
#include "window_border.h"
extern "C" BOOL WINAPI c_AdjustWindowRectEx(RECT* r,DWORD style,BOOL menu,DWORD ex){
 if(!r)return FALSE;
 // Wine adjusts requested nonclient geometry even when WS_MINIMIZE is set.
 LONG cx=classic_window_border(style,ex),cy=cx;
 r->left-=cx;r->top-=cy;r->right+=cx;r->bottom+=cy;
 r->top-=classic_caption_height(style,ex);
 if(menu)r->top-=20;
 if(ex&WS_EX_CLIENTEDGE){r->left-=2;r->top-=2;r->right+=2;r->bottom+=2;}
 return TRUE;
}
extern "C" BOOL WINAPI c_AdjustWindowRect(RECT* r,DWORD style,BOOL menu){return c_AdjustWindowRectEx(r,style,menu,0);}

extern "C" BOOL WINAPI c_SetWindowPos(HWND h,HWND,int x,int y,int cx,int cy,UINT flags){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto w=window(h);if(!w)return FALSE;
 LONG nx=(flags&SWP_NOMOVE)?w->x:x, ny=(flags&SWP_NOMOVE)?w->y:y;
 LONG ncx=(flags&SWP_NOSIZE)?w->cx:std::max(0,cx), ncy=(flags&SWP_NOSIZE)?w->cy:std::max(0,cy);
 bool moved=nx!=w->x||ny!=w->y, sized=ncx!=w->cx||ncy!=w->cy;
 w->x=nx;w->y=ny;w->cx=ncx;w->cy=ncy;
 if(flags&SWP_SHOWWINDOW)w->style|=WS_VISIBLE;
 if(flags&SWP_HIDEWINDOW)w->style&=~WS_VISIBLE;
 // No z-order tracking (insertAfter ignored), same limitation as EnumWindows.
 if(moved&&w->alive)w->proc(h,WM_MOVE,0,MAKELPARAM(nx,ny));
 if(sized&&w->alive)w->proc(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(clientWidth(w),clientHeight(w)));
 if(moved||sized)w->needSize=false;
 else if((flags&SWP_SHOWWINDOW)&&w->alive)sendInitialSizeMove(h,w);
 return TRUE;
}
extern "C" BOOL WINAPI c_ValidateRect(HWND h,const RECT*){std::lock_guard<std::recursive_mutex> l(mutex);return window(h)!=nullptr;} // no invalid-region tracking: nothing to validate, trivially honest.
extern "C" BOOL WINAPI c_UpdateWindow(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);return window(h)!=nullptr;} // no pending update region ever exists here, so nothing to flush.
// InvalidateRect: no invalid region is tracked (see ValidateRect), so there is
// nothing to add and no WM_PAINT to schedule. NULL means every window (TRUE).
extern "C" BOOL WINAPI c_InvalidateRect(HWND h,const RECT*,BOOL){std::lock_guard<std::recursive_mutex> l(mutex);if(h&&!window(h)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}return TRUE;}

// Icon/cursor handles and device contexts. Wine/Windows route HICON and
// HCURSOR through the same shared handle table (user32/cursoricon.c); this
// record module mirrors that with one allocator. No GDI bitmap backend
// exists here, so hbmMask/hbmColor from GetIconInfo and all pixel/codec
// decoding (file loads, .ani, LR_LOADFROMFILE) are honestly unimplemented
// rather than fabricated.
#ifndef IMAGE_BITMAP
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#endif
#ifndef LR_MONOCHROME
#define LR_MONOCHROME 0x0001
#define LR_LOADFROMFILE 0x0010
#define LR_DEFAULTSIZE 0x0040
#endif
struct IconInfo2{BOOL fIcon;DWORD xHotspot,yHotspot;HBITMAP hbmMask,hbmColor;};
struct IconRec{bool isIcon;int cx,cy;POINT hotspot;bool shared;};
static std::vector<std::unique_ptr<IconRec>> icons;
static std::map<int,HICON> sysIcons; static std::map<int,HCURSOR> sysCursors;
static IconRec* iconRec(HANDLE h){for(auto& i:icons)if((HANDLE)i.get()==h)return i.get();return nullptr;}
static HICON allocIcon(bool isIcon,int cx,int cy,POINT hotspot,bool shared){auto r=std::make_unique<IconRec>();r->isIcon=isIcon;r->cx=cx?cx:32;r->cy=cy?cy:32;r->hotspot=hotspot;r->shared=shared;HICON h=(HICON)r.get();icons.push_back(std::move(r));return h;}
struct DCRec{HWND hwnd;};
static std::vector<std::unique_ptr<DCRec>> dcs;
extern "C" HDC WINAPI c_GetDC(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);if(h&&!window(h))return nullptr;auto d=std::make_unique<DCRec>();d->hwnd=h;HDC handle=(HDC)d.get();dcs.push_back(std::move(d));return handle;} // opaque per-call handle; no real GDI surface backs it (no rendering in this module).
// Validate guest DC identity before exposing virtual-display metrics.
extern "C" int WINAPI uwph_u32c_device_caps(HDC hdc,int cap){
 std::lock_guard<std::recursive_mutex> l(mutex);
 for(const auto& dc:dcs)if((HDC)dc.get()==hdc)
  return gdi32_core::display_cap(cap,desktopWidth,desktopHeight);
 SetLastError(ERROR_INVALID_HANDLE);return 0;
}
extern "C" HDC WINAPI c_GetWindowDC(HWND h){return c_GetDC(h);} // same opaque record DC as GetDC; Mesa's WGL presenter imports it (never drawn on: no GDI surface)
extern "C" int WINAPI c_ReleaseDC(HWND h,HDC hdc){std::lock_guard<std::recursive_mutex> l(mutex);for(size_t i=0;i<dcs.size();i++)if((HDC)dcs[i].get()==hdc&&dcs[i]->hwnd==h){dcs.erase(dcs.begin()+i);return 1;}return 0;}
extern "C" HANDLE WINAPI c_LoadImageW(HINSTANCE inst,LPCWSTR name,UINT type,int cx,int cy,UINT flags){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(flags&LR_LOADFROMFILE){SetLastError(ERROR_FILE_NOT_FOUND);return nullptr;} // no file/codec backend: honest failure, not fabricated.
 if(type==IMAGE_BITMAP){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;} // no GDI bitmap backend in this module.
 if(inst||!integer(name)){SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);return nullptr;} // no PE resource-section parser for module-supplied icons/cursors: documented limitation.
 int id=(int)(ULONG_PTR)name;
 if(type==IMAGE_CURSOR){auto it=sysCursors.find(id);if(it!=sysCursors.end())return it->second;return sysCursors[id]=allocIcon(false,cx,cy,{16,16},true);}
 auto it=sysIcons.find(id);if(it!=sysIcons.end())return it->second;int w=cx?cx:32,hh=cy?cy:32;return sysIcons[id]=allocIcon(true,w,hh,{w/2,hh/2},true);
}
extern "C" HANDLE WINAPI c_LoadImageA(HINSTANCE inst,LPCSTR name,UINT type,int cx,int cy,UINT flags){auto w=wide(name);return c_LoadImageW(inst,integer(name)?(LPCWSTR)name:w.c_str(),type,cx,cy,flags);}
// Wine's LoadCursorA/LoadIconA/LoadIconW literally call LoadImage with LR_SHARED|LR_DEFAULTSIZE; mirrored here.
extern "C" HCURSOR WINAPI c_LoadCursorA(HINSTANCE inst,LPCSTR name){return (HCURSOR)c_LoadImageA(inst,name,IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);}
extern "C" HICON WINAPI c_LoadIconA(HINSTANCE inst,LPCSTR name){return (HICON)c_LoadImageA(inst,name,IMAGE_ICON,0,0,LR_DEFAULTSIZE);}
extern "C" HICON WINAPI c_LoadIconW(HINSTANCE inst,LPCWSTR name){return (HICON)c_LoadImageW(inst,name,IMAGE_ICON,0,0,LR_DEFAULTSIZE);}
extern "C" BOOL WINAPI c_DestroyIcon(HICON h){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto it=std::find_if(icons.begin(),icons.end(),[&](auto& i){return (HANDLE)i.get()==(HANDLE)h;});
 if(it==icons.end()){SetLastError(ERROR_INVALID_CURSOR_HANDLE);return FALSE;}
 if((*it)->shared)return TRUE; // shared/system icons and cursors are never actually freed, same as real Windows.
 icons.erase(it);return TRUE;
}
extern "C" BOOL WINAPI c_DestroyCursor(HCURSOR h){return c_DestroyIcon((HICON)h);} // same shared icon/cursor handle table.
// CopyImage for icon/cursor records: a new, caller-owned record of the requested
// size (0 keeps the original), as Windows returns a distinct handle even for an
// unchanged copy. LR_COPYDELETEORG destroys a non-shared original. Bitmaps have
// no backend in this module (as LoadImage IMAGE_BITMAP).
extern "C" HANDLE WINAPI c_CopyImage(HANDLE h,UINT type,int cx,int cy,UINT flags){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(type==IMAGE_BITMAP){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
 if(type!=IMAGE_ICON&&type!=IMAGE_CURSOR){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
 auto r=iconRec(h);
 if(!r){SetLastError(ERROR_INVALID_CURSOR_HANDLE);return nullptr;}
 IconRec source=*r;
 int w=cx?cx:source.cx,hh=cy?cy:source.cy;
 POINT hotspot=source.hotspot;
 if(source.cx&&source.cy){hotspot.x=hotspot.x*w/source.cx;hotspot.y=hotspot.y*hh/source.cy;}
 HICON copy=allocIcon(type==IMAGE_ICON,w,hh,hotspot,false);
 if((flags&LR_COPYDELETEORG)&&!source.shared)c_DestroyIcon((HICON)h);
 return copy;
}
extern "C" BOOL WINAPI c_GetIconInfo(HICON h,IconInfo2* info){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto r=iconRec((HANDLE)h);
 if(!r||!info){SetLastError(ERROR_INVALID_CURSOR_HANDLE);return FALSE;}
 info->fIcon=r->isIcon;info->xHotspot=(DWORD)r->hotspot.x;info->yHotspot=(DWORD)r->hotspot.y;
 info->hbmMask=nullptr;info->hbmColor=nullptr; // real Windows always returns caller-owned bitmaps here; no GDI bitmap backend exists in this module (documented gap, not fabricated).
 return TRUE;
}
extern "C" HICON WINAPI c_CreateIconFromResourceEx(BYTE* bits,DWORD cbSize,BOOL isIcon,DWORD version,int width,int height,UINT){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(!bits||version==0x00020000){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;} // 2.xx resources: unsupported in Wine too.
 if(cbSize>=4&&!memcmp(bits,"RIFF",4)){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;} // animated .ani resources: no decoder here.
 // Validate the declared resource length before reading either the bitmap
 // dimensions or the cursor hotspot prefix.
 if(cbSize<sizeof(BITMAPINFOHEADER)+(isIcon?0:4)){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
 POINT hotspot{0,0};const BITMAPINFOHEADER* bmi;
 if(isIcon)bmi=(const BITMAPINFOHEADER*)bits;
 else{auto pt=(const SHORT*)bits;hotspot={pt[0],pt[1]};bmi=(const BITMAPINFOHEADER*)(pt+2);}
 int cx=width,cy=height;
 if(!cx)cx=bmi->biWidth;
 if(!cy)cy=std::abs((long)bmi->biHeight)/(isIcon?2:1); // icon DIBs stack XOR+AND masks, doubling biHeight; matches Wine's create_icon_from_bmi halving.
 if(isIcon)hotspot={cx/2,cy/2};
 return allocIcon(isIcon,cx,cy,hotspot,false);
}
#pragma pack(push,1)
struct IconResDir2{BYTE bWidth,bHeight,bColorCount,bReserved;};
struct CursorDir2{WORD wWidth,wHeight;};
struct CursorIconDirEntry2{union{IconResDir2 icon;CursorDir2 cursor;} info;WORD wPlanes,wBitCount;DWORD dwBytesInRes;WORD wResId;};
struct CursorIconDir2{WORD idReserved,idType,idCount;CursorIconDirEntry2 idEntries[1];};
#pragma pack(pop)
static_assert(sizeof(CursorIconDirEntry2)==14,"CURSORICONDIRENTRY layout");
// Ports Wine's CURSORICON_FindBestIcon/CURSORICON_FindBestCursor (user32/cursoricon.c)
// byte-for-byte against caller-supplied directory bytes: a pure computation,
// not a backend-dependent call, so it is implemented for real rather than stubbed.
extern "C" int WINAPI c_LookupIconIdFromDirectoryEx(BYTE* xdir,BOOL isIcon,int width,int height,UINT flags){
 auto dir=(const CursorIconDir2*)xdir;
 if(!dir||dir->idReserved||!(dir->idType&3))return 0;
 int depth=(flags&LR_MONOCHROME)?1:32; // no real display depth in this module: assume 32bpp (modern default), not fabricated per-mode detail.
 if(!width)width=32; if(!height)height=32; // this API always implies LR_DEFAULTSIZE; no real SM_CXICON/SM_CXCURSOR here, so 32x32 (Windows' common default).
 int n=dir->idCount,best=-1;
 if(isIcon){
  unsigned iTotalDiff=~0u,iXDiff=0,iYDiff=0,iColorDiff=~0u;
  for(int i=0;i<n;i++){auto& e=dir->idEntries[i];int cx=e.info.icon.bWidth?e.info.icon.bWidth:256,cy=e.info.icon.bHeight?e.info.icon.bHeight:256;
   unsigned dx=(unsigned)std::abs(width-cx),dy=(unsigned)std::abs(height-cy);
   if(iTotalDiff>dx+dy){iXDiff=dx;iYDiff=dy;iTotalDiff=dx+dy;}}
  for(int i=0;i<n;i++){auto& e=dir->idEntries[i];int cx=e.info.icon.bWidth?e.info.icon.bWidth:256,cy=e.info.icon.bHeight?e.info.icon.bHeight:256;
   if((unsigned)std::abs(width-cx)==iXDiff&&(unsigned)std::abs(height-cy)==iYDiff){unsigned cd=(unsigned)std::abs(depth-(int)e.wBitCount);if(iColorDiff>cd){best=i;iColorDiff=cd;}}}
 } else {
  int maxw=0,maxh=0,maxbits=0;
  for(int i=0;i<n;i++){auto& e=dir->idEntries[i];int cx=e.info.cursor.wWidth,cy=e.info.cursor.wHeight;if(cy==cx*2)cy/=2;int bits=e.wBitCount;
   if(cx>width||cy>height)continue; if(cx<maxw||cy<maxh)continue;
   if(cx==maxw&&cy==maxh){if(flags&LR_MONOCHROME){if(maxbits&&bits>=maxbits)continue;}else if(bits<=maxbits)continue;}
   best=i;maxw=cx;maxh=cy;maxbits=bits;}
  if(best<0){maxw=maxh=255;
   for(int i=0;i<n;i++){auto& e=dir->idEntries[i];int cx=e.info.cursor.wWidth,cy=e.info.cursor.wHeight;if(cy==cx*2)cy/=2;int bits=e.wBitCount;
    if(cx>maxw||cy>maxh)continue;
    if(cx==maxw&&cy==maxh){if(flags&LR_MONOCHROME){if(maxbits&&bits>=maxbits)continue;}else if(bits<=maxbits)continue;}
    best=i;maxw=cx;maxh=cy;maxbits=bits;}
   if(best<0)best=0;}
 }
 return best>=0?dir->idEntries[best].wResId:0;
}
#include "message_queue.h"
#include "dialog_keys.h"
#include "dpi.h"
#include "device_notifications.h"
#include "input_feed.h"
#include "window_extras.h"
#include "window_tree.h"
} // namespace u32c
