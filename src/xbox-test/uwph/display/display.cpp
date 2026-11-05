// Private single-UI-thread window adapter. Opaque HWNDs never reach OS user32.
#include <windows.h>
#include <xinput.h>
#include <unknwn.h>
using WNDPROC=LRESULT (CALLBACK*)(HWND,UINT,WPARAM,LPARAM);
struct WNDCLASSEXW { UINT cbSize,style; WNDPROC lpfnWndProc; int cbClsExtra,cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName,lpszClassName; HICON hIconSm; };
struct CREATESTRUCTW { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy,cx,y,x; LONG style; LPCWSTR lpszName,lpszClass; DWORD dwExStyle; };
#include <deque>
#include <string>
#include <vector>
static_assert(sizeof(WNDCLASSEXW)==80 && sizeof(CREATESTRUCTW)==80,"x64 Win32 layouts");
struct Class { std::wstring name; WNDPROC proc; ATOM atom; };
struct Window { WNDPROC proc; bool alive; LONG width,height; bool destroying=false; };
static std::vector<Class> classes;
static std::vector<Window*> windows;
static std::deque<MSG> queue;
static IUnknown* core;
static void (*pump)();
static LONG width,height;
static HWND active;
static DWORD ui_thread;
static SRWLOCK pad_lock=SRWLOCK_INIT;
static XINPUT_STATE pads[4]{};
static bool connected[4]{};
static bool valid(HWND h) { for(auto w:windows) if((HWND)w==h) return w->alive; return false; }
static bool on_ui_thread() { if(ui_thread && GetCurrentThreadId()==ui_thread)return true;SetLastError(ERROR_INVALID_THREAD_ID);return false; }
static void post(HWND h,UINT m,WPARAM w=0,LPARAM l=0) { MSG msg{}; msg.hwnd=h;msg.message=m;msg.wParam=w;msg.lParam=l;queue.push_back(msg); }
extern "C" __declspec(dllexport) void uwph_display_init(IUnknown* c,void(*p)(),LONG x,LONG y) { ui_thread=GetCurrentThreadId();core=c;pump=p;width=x;height=y; }
extern "C" __declspec(dllexport) void uwph_display_event(UINT m,WPARAM w,LPARAM l) {
 if(!on_ui_thread())return;
 if(m==WM_SIZE){width=LOWORD(l);height=HIWORD(l);}
 // A CoreWindow has one input target. Resizes affect all mapped top-levels.
 for(auto h:windows) if(h->alive&&!h->destroying){if(m==WM_SIZE){h->width=width;h->height=height;post((HWND)h,m,w,l);}else if((HWND)h==active)post((HWND)h,m,w,l);}
}
extern "C" __declspec(dllexport) void uwph_display_pad(DWORD i,const XINPUT_GAMEPAD* p) {
 if(!on_ui_thread()||i>=4)return; AcquireSRWLockExclusive(&pad_lock); bool was_connected=connected[i];connected[i]=p!=nullptr; if(p){if(!was_connected||memcmp(&pads[i].Gamepad,p,sizeof(*p))){++pads[i].dwPacketNumber;pads[i].Gamepad=*p;}}ReleaseSRWLockExclusive(&pad_lock);
}
extern "C" __declspec(dllexport) IUnknown* uwph_corewindow(HWND h) { if(!on_ui_thread()||!valid(h)||!core)return nullptr;core->AddRef();return core; }
extern "C" ATOM WINAPI u_RegisterClassExW(const WNDCLASSEXW* c){if(!on_ui_thread())return 0;if(!c||c->cbSize!=sizeof(*c)||!c->lpfnWndProc||ULONG_PTR(c->lpszClassName)<=65535||c->cbClsExtra<0||c->cbWndExtra<0){SetLastError(ERROR_INVALID_PARAMETER);return 0;}for(auto& a:classes)if(a.name==c->lpszClassName){SetLastError(ERROR_CLASS_ALREADY_EXISTS);return 0;}if(classes.size()>=65535){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}ATOM a=(ATOM)(classes.size()+1);classes.push_back({c->lpszClassName,c->lpfnWndProc,a});return a;}
extern "C" BOOL WINAPI u_DestroyWindow(HWND h);
extern "C" LRESULT WINAPI u_DefWindowProcW(HWND h,UINT m,WPARAM,LPARAM){if(!on_ui_thread())return 0;if(m==WM_CLOSE)u_DestroyWindow(h);return m==WM_NCCREATE?TRUE:0;}
extern "C" HWND WINAPI u_CreateWindowExW(DWORD ex,LPCWSTR name,LPCWSTR title,DWORD style,int x,int y,int cx,int cy,HWND parent,HMENU menu,HINSTANCE inst,LPVOID param){
 if(!on_ui_thread())return nullptr;if(parent||(style&WS_CHILD)){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}Class* c=nullptr;for(auto& a:classes)if((ULONG_PTR(name)<=65535 && a.atom==(ATOM)(ULONG_PTR)name)||(ULONG_PTR(name)>65535&&a.name==name)){c=&a;break;}if(!c){SetLastError(ERROR_CANNOT_FIND_WND_CLASS);return nullptr;}
 auto h=new Window{c->proc,true,width,height};windows.push_back(h);HWND hwnd=(HWND)h;CREATESTRUCTW cs{param,inst,menu,parent,cy,cx,y,x,(LONG)style,title,name,ex};
 if(!h->proc(hwnd,WM_NCCREATE,0,(LPARAM)&cs)||!h->alive){h->alive=false;return nullptr;}if(h->proc(hwnd,WM_CREATE,0,(LPARAM)&cs)==-1){u_DestroyWindow(hwnd);return nullptr;}if(!h->alive)return nullptr;if(valid(active))post(active,WM_ACTIVATE,WA_INACTIVE);active=hwnd;post(hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width,height));post(hwnd,WM_ACTIVATE,WA_ACTIVE);post(hwnd,WM_PAINT);return hwnd;
}
extern "C" BOOL WINAPI u_PeekMessageW(MSG* out,HWND h,UINT lo,UINT hi,UINT remove){if(!on_ui_thread()||!out)return FALSE;if(pump)pump();for(auto i=queue.begin();i!=queue.end();++i){if((i->message==WM_QUIT||((!h||i->hwnd==h)&&((!lo&&!hi)||(i->message>=lo&&i->message<=hi))))){*out=*i;if(remove&PM_REMOVE)queue.erase(i);return TRUE;}}return FALSE;}
extern "C" BOOL WINAPI u_GetMessageW(MSG* out,HWND h,UINT lo,UINT hi){if(!on_ui_thread())return -1;if(!out||(h&&!valid(h))){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return -1;}while(!u_PeekMessageW(out,h,lo,hi,PM_REMOVE))Sleep(1);return out->message!=WM_QUIT;}
extern "C" LRESULT WINAPI u_DispatchMessageW(const MSG* m){return on_ui_thread()&&m&&valid(m->hwnd)?((Window*)m->hwnd)->proc(m->hwnd,m->message,m->wParam,m->lParam):0;}
extern "C" BOOL WINAPI u_TranslateMessage(const MSG*){return on_ui_thread();}
extern "C" void WINAPI u_PostQuitMessage(int code){if(on_ui_thread())post(nullptr,WM_QUIT,code);}
extern "C" BOOL WINAPI u_PostMessageW(HWND h,UINT m,WPARAM w,LPARAM l){if(!on_ui_thread()||!valid(h))return FALSE;post(h,m,w,l);return TRUE;}
extern "C" BOOL WINAPI u_GetClientRect(HWND h,RECT* r){if(!on_ui_thread()||!valid(h)||!r)return FALSE;*r={0,0,((Window*)h)->width,((Window*)h)->height};return TRUE;}
extern "C" BOOL WINAPI u_ShowWindow(HWND h,int){return on_ui_thread()&&valid(h);}
extern "C" BOOL WINAPI u_DestroyWindow(HWND h){if(!on_ui_thread()||!valid(h))return FALSE;auto w=(Window*)h;if(w->destroying)return FALSE;w->destroying=true;w->proc(h,WM_DESTROY,0,0);w->proc(h,WM_NCDESTROY,0,0);w->alive=false;for(auto i=queue.begin();i!=queue.end();)if(i->hwnd==h)i=queue.erase(i);else ++i;if(active==h){active=nullptr;for(auto i=windows.rbegin();i!=windows.rend();++i)if((*i)->alive&&!(*i)->destroying){active=(HWND)*i;post(active,WM_ACTIVATE,WA_ACTIVE);break;}}return TRUE;}
// Games commonly poll XInput on a worker; publish coherent snapshots from UI/WGI.
extern "C" DWORD WINAPI u_XInputGetState(DWORD i,XINPUT_STATE* s){
 if(i>=4||!s)return ERROR_BAD_ARGUMENTS;
 AcquireSRWLockShared(&pad_lock);
 DWORD result=connected[i]?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
 if(result==ERROR_SUCCESS)*s=pads[i];
 ReleaseSRWLockShared(&pad_lock);
 return result;
}
// Entry-import adapters for the single CoreWindow's opaque top-level HWNDs.
struct EntryWndClassW { UINT style; WNDPROC proc; int clsExtra,wndExtra; HINSTANCE instance; HICON icon; HCURSOR cursor; HBRUSH brush; LPCWSTR menu,name; };
extern "C" ATOM WINAPI u_RegisterClassW(const EntryWndClassW* c) {
 if(!c){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 WNDCLASSEXW x{sizeof(x),c->style,c->proc,c->clsExtra,c->wndExtra,c->instance,c->icon,c->cursor,c->brush,c->menu,c->name,nullptr};
 return u_RegisterClassExW(&x);
}
extern "C" HWND WINAPI u_GetActiveWindow(){return on_ui_thread()?active:nullptr;}
extern "C" HWND WINAPI u_GetFocus(){return u_GetActiveWindow();}
extern "C" HWND WINAPI u_GetForegroundWindow(){return u_GetActiveWindow();}
extern "C" BOOL WINAPI u_GetWindowRect(HWND h,RECT* r){return u_GetClientRect(h,r);}
extern "C" BOOL WINAPI u_IsWindowVisible(HWND h){return on_ui_thread()&&valid(h);}
extern "C" BOOL WINAPI u_ClientToScreen(HWND h,POINT* p){return on_ui_thread()&&valid(h)&&p;}
extern "C" BOOL WINAPI u_ScreenToClient(HWND h,POINT* p){return u_ClientToScreen(h,p);}
extern "C" BOOL WINAPI u_AdjustWindowRectEx(RECT* r,DWORD,BOOL,DWORD){return r!=nullptr;}
extern "C" HWND WINAPI u_SetFocus(HWND h){if(!on_ui_thread()||!valid(h))return nullptr;HWND old=active;active=h;return old;}
extern "C" BOOL WINAPI u_SetForegroundWindow(HWND h){if(!on_ui_thread()||!valid(h))return FALSE;active=h;return TRUE;}
