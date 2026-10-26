// Record-only user32 subset. No OS HWND, CoreWindow, device notifications or rendering.
// Wine user32/*.spec, user32/win.c and win32u/{class,window}.c define the ABI/semantics.
#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
namespace u32c {
using Proc=LRESULT(CALLBACK*)(HWND,UINT,WPARAM,LPARAM);
struct ClassEx {UINT size,style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCWSTR menu,name;HICON smallIcon;};
struct ClassW {UINT style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCWSTR menu,name;};
struct Create {void* param;HINSTANCE instance;HMENU menu;HWND parent;int cy,cx,y,x;LONG style;LPCWSTR title,name;DWORD ex;};
static_assert(sizeof(ClassEx)==80 && sizeof(Create)==80,"Win64 ABI");
struct Class {ClassEx info;std::wstring name,menu;ATOM atom;std::vector<BYTE> extra;};
struct Window {Class* cls;Proc proc;DWORD thread;bool alive=true,destroying=false;LONG_PTR user=0,id=0;HINSTANCE instance;HWND parent;LONG style,ex;int x,y,cx,cy;std::wstring title;std::vector<BYTE> extra;std::map<std::wstring,HANDLE> props;};
static Window* desktop;
static std::recursive_mutex mutex;
static std::vector<std::unique_ptr<Class>> classes;
static std::vector<std::unique_ptr<Window>> windows;
struct Atom {std::wstring name;unsigned refs;};
static std::map<ATOM,Atom> atoms;
static unsigned nextAtom=0xc000;
static bool integer(const void* p){return ULONG_PTR(p)<=0xffff;}
static std::wstring fold(std::wstring s){for(auto& c:s)if(c>=L'A'&&c<=L'Z')c+=32;return s;}
static Window* window(HWND h){for(auto& w:windows)if((HWND)w.get()==h&&w->alive)return w.get();SetLastError(ERROR_INVALID_WINDOW_HANDLE);return nullptr;}
static Class* klass(LPCWSTR name,HINSTANCE instance){for(auto& c:classes)if((integer(name)?c->atom==LOWORD(ULONG_PTR(name)):fold(c->name)==fold(name))&&(c->info.instance==instance||(c->info.style&0x4000)))return c.get();return nullptr;}
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
extern "C" HWND WINAPI c_CreateWindowExW(DWORD ex,LPCWSTR n,LPCWSTR title,DWORD style,int x,int y,int cx,int cy,HWND parent,HMENU menu,HINSTANCE i,void* param){std::lock_guard<std::recursive_mutex> l(mutex);auto c=klass(n,i);if(!c){SetLastError(ERROR_CANNOT_FIND_WND_CLASS);return nullptr;}if(parent&&!window(parent))return nullptr;if(windows.size()>=4096){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}auto v=std::make_unique<Window>();v->cls=c;v->proc=c->info.proc;v->thread=GetCurrentThreadId();v->instance=i;v->parent=parent;v->id=(LONG_PTR)menu;v->style=style;v->ex=ex;v->x=x==CW_USEDEFAULT?0:x;v->y=y==CW_USEDEFAULT?0:y;v->cx=cx==CW_USEDEFAULT?640:std::max(0,cx);v->cy=cy==CW_USEDEFAULT?480:std::max(0,cy);v->title=title?title:L"";v->extra.resize(c->info.wndExtra);auto p=v.get();HWND h=(HWND)p;windows.push_back(std::move(v));Create cs{param,i,menu,parent,cy,cx,y,x,(LONG)style,title,n,ex};if(!p->proc(h,WM_NCCREATE,0,(LPARAM)&cs)){if(p->alive)p->proc(h,WM_NCDESTROY,0,0);p->alive=false;p->props.clear();return nullptr;}if(!p->alive)return nullptr;if(p->proc(h,WM_CREATE,0,(LPARAM)&cs)==-1){c_DestroyWindow(h);return nullptr;}return p->alive?h:nullptr;}
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
extern "C" BOOL WINAPI c_ShowWindow(HWND h,int cmd){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;BOOL old=(w->style&WS_VISIBLE)!=0;if(cmd==SW_HIDE)w->style&=~WS_VISIBLE;else w->style|=WS_VISIBLE;if(cmd==SW_MINIMIZE||cmd==SW_SHOWMINIMIZED)w->style|=WS_MINIMIZE;if(cmd==SW_RESTORE)w->style&=~(WS_MINIMIZE|WS_MAXIMIZE);return old;}
extern "C" BOOL WINAPI c_GetClientRect(HWND h,RECT* r){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!r)return FALSE;*r={0,0,w->cx,w->cy};return TRUE;}
static bool origin(HWND h,LONG& x,LONG& y){for(unsigned depth=0;h&&depth<4096;++depth){auto w=window(h);if(!w)return false;x+=w->x;y+=w->y;if(!(w->style&WS_CHILD))return true;h=w->parent;}return !h;}
extern "C" BOOL WINAPI c_ClientToScreen(HWND h,POINT* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p)return FALSE;LONG x=0,y=0;if(!origin(h,x,y))return FALSE;p->x+=x;p->y+=y;return TRUE;}
extern "C" BOOL WINAPI c_ScreenToClient(HWND h,POINT* p){std::lock_guard<std::recursive_mutex> l(mutex);if(!p)return FALSE;LONG x=0,y=0;if(!origin(h,x,y))return FALSE;p->x-=x;p->y-=y;return TRUE;}
extern "C" BOOL WINAPI c_GetWindowRect(HWND h,RECT* r){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||!r)return FALSE;LONG x=0,y=0;if(!origin(h,x,y))return FALSE;*r={x,y,x+w->cx,y+w->cy};return TRUE;}
extern "C" LRESULT WINAPI c_SendMessageW(HWND h,UINT m,WPARAM w,LPARAM p){std::lock_guard<std::recursive_mutex> l(mutex);auto v=window(h);if(!v)return 0;if(v->thread!=GetCurrentThreadId()){SetLastError(ERROR_NOT_SUPPORTED);return 0;}return v->proc(h,m,w,p);}
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
extern "C" BOOL WINAPI c_EnumWindows(EnumProc proc,LPARAM p){if(!proc){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}std::vector<HWND> snapshot;{std::lock_guard<std::recursive_mutex> l(mutex);for(auto& w:windows)if(w->alive&&w.get()!=desktop&&!(w->style&WS_CHILD))snapshot.push_back((HWND)w.get());}for(auto h:snapshot)if(c_IsWindow(h)&&!proc(h,p))return FALSE;return TRUE;}
extern "C" HWND WINAPI c_GetDlgItem(HWND h,int id){std::lock_guard<std::recursive_mutex> l(mutex);if(!window(h))return nullptr;for(auto& w:windows)if(w->alive&&w->parent==h&&w->id==id)return (HWND)w.get();return nullptr;}
extern "C" LRESULT WINAPI c_SendDlgItemMessageW(HWND h,int id,UINT m,WPARAM w,LPARAM p){return c_SendMessageW(c_GetDlgItem(h,id),m,w,p);}
extern "C" BOOL WINAPI c_SetWindowTextW(HWND h,LPCWSTR s){return (BOOL)c_SendMessageW(h,WM_SETTEXT,0,(LPARAM)s);}
extern "C" BOOL WINAPI c_SetDlgItemTextW(HWND h,int id,LPCWSTR s){return c_SetWindowTextW(c_GetDlgItem(h,id),s);}
extern "C" BOOL WINAPI c_MoveWindow(HWND h,int x,int y,int cx,int cy,BOOL){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;w->x=x;w->y=y;w->cx=std::max(0,cx);w->cy=std::max(0,cy);w->proc(h,WM_MOVE,0,MAKELPARAM(x,y));if(w->alive)w->proc(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(w->cx,w->cy));return TRUE;}
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
extern "C" BOOL WINAPI c_GetMonitorInfoW(HMONITOR h,MonitorInfo* p){std::lock_guard<std::recursive_mutex> l(mutex);if(h!=(HMONITOR)&monitorToken||!p||(p->size!=sizeof(MonitorInfo)&&p->size!=sizeof(MonitorInfoW))){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}p->monitor=p->work=RECT{0,0,desktopWidth,desktopHeight};p->flags=1;if(p->size==sizeof(MonitorInfoW))wcscpy(((MonitorInfoW*)p)->device,L"U32C-Record-Display");return TRUE;}
extern "C" BOOL WINAPI c_GetMonitorInfoA(HMONITOR h,MonitorInfo* p){if(!p||(p->size!=sizeof(MonitorInfo)&&p->size!=sizeof(MonitorInfoA))){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}MonitorInfo w{sizeof(w)};if(!c_GetMonitorInfoW(h,&w))return FALSE;p->monitor=w.monitor;p->work=w.work;p->flags=w.flags;if(p->size==sizeof(MonitorInfoA))strcpy(((MonitorInfoA*)p)->device,"U32C-Record-Display");return TRUE;}
extern "C" int WINAPI c_GetSystemMetrics(int n){std::lock_guard<std::recursive_mutex> l(mutex);switch(n){case SM_CXSCREEN:case SM_CXVIRTUALSCREEN:return desktopWidth;case SM_CYSCREEN:case SM_CYVIRTUALSCREEN:return desktopHeight;case SM_CMONITORS:case SM_SAMEDISPLAYFORMAT:return 1;case SM_XVIRTUALSCREEN:case SM_YVIRTUALSCREEN:return 0;default:SetLastError(ERROR_NOT_SUPPORTED);return 0;}}
} // namespace u32c
