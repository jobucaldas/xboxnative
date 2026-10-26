// Own trusted regression PE. Game images remain non-executing.
#include <windows.h>
static const char* volatile anchor="U32C mapped regression PE";
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_add(LPCWSTR);
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_find(LPCWSTR);
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_delete(ATOM);
static int checks,failures,created,destroyed;
static void check_result(bool ok,const char* expression){++checks;if(!ok){++failures;OutputDebugStringA(expression);}}
#define check(...) check_result((__VA_ARGS__),"U32C CHECK FAIL " #__VA_ARGS__)
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM p){
 if(m==WM_NCCREATE){auto c=(CREATESTRUCTW*)p;check(c->lpCreateParams==(void*)0x12345678);SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)0x1234567887654321ULL);++created;}
 if(m==WM_NCDESTROY)++destroyed;
 return DefWindowProcW(h,m,w,p);
}
static LRESULT CALLBACK subclass(HWND h,UINT m,WPARAM w,LPARAM p){if(m==WM_APP)return 42;return CallWindowProcW(proc,h,m,w,p);}
static int rejectedDestroyed,rejectedCreated;
static LRESULT CALLBACK reject(HWND,UINT m,WPARAM,LPARAM){if(m==WM_NCDESTROY)++rejectedDestroyed;if(m==WM_CREATE)++rejectedCreated;return 0;}
static BOOL CALLBACK count(HWND,LPARAM p){++*(int*)p;return TRUE;}
extern "C" DWORD WINAPI u32c_entry(){
 OutputDebugStringA(anchor);
 WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=proc;wc.lpszClassName=L"U32cClass";wc.cbWndExtra=16;wc.cbClsExtra=8;
 ATOM a=RegisterClassExW(&wc);check(a!=0);
 check(!RegisterClassExW(&wc)&&GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
 WNDCLASSEXW info={};info.cbSize=sizeof(info);check(GetClassInfoExW(0,L"u32cclass",&info)&&info.cbWndExtra==16&&info.lpfnWndProc==proc);
 HWND h=CreateWindowExW(0,MAKEINTATOM(a),L"first",WS_VISIBLE,23,47,320,240,0,0,0,(void*)0x12345678);
 check(h&&created==1);check(GetWindowLongPtrW(h,GWLP_USERDATA)==(LONG_PTR)0x1234567887654321ULL);
 check(SetWindowLongPtrW(h,GWLP_USERDATA,7)==(LONG_PTR)0x1234567887654321ULL);check(GetWindowLongPtrW(h,GWLP_USERDATA)==7);
 if(!failures)OutputDebugStringA("U32C RegisterClassExW/CreateWindowExW/GetWindowLongPtrW roundtrip OK");
 check(SetWindowLongPtrW(h,0,(LONG_PTR)0x76543210abcdef01ULL)==0);check(GetWindowLongPtrW(h,0)==(LONG_PTR)0x76543210abcdef01ULL);
 check(GetWindowLongA(h,0)==(LONG)0xabcdef01);SetLastError(0);check(GetWindowLongPtrW(h,12)==0&&GetLastError()==ERROR_INVALID_INDEX);
 check(!UnregisterClassW(wc.lpszClassName,0)&&GetLastError()==ERROR_CLASS_HAS_WINDOWS);
 check(SetPropW(h,L"CaseSensitive",(HANDLE)0x1234));check(GetPropA(h,"CaseSensitive")== (HANDLE)0x1234);check(GetPropW(h,L"casesensitive")== (HANDLE)0x1234);
 check(SetPropW(h,MAKEINTATOM(123),(HANDLE)0x5678));check(GetPropW(h,MAKEINTATOM(123))==(HANDLE)0x5678);
 ATOM prop=uwph_atom_add(L"AtomProperty");check(prop>=0xc000);check(uwph_atom_find(L"atomproperty")==prop);check(SetPropW(h,MAKEINTATOM(prop),(HANDLE)0x7777));check(GetPropW(h,L"AtomProperty")== (HANDLE)0x7777);check(RemovePropW(h,L"atomproperty")== (HANDLE)0x7777);check(!uwph_atom_delete(prop));
 check(RemovePropW(h,L"CaseSensitive")== (HANDLE)0x1234);check(!GetPropW(h,L"CaseSensitive"));
 check(SetWindowLongPtrW(h,GWLP_WNDPROC,(LONG_PTR)subclass)==(LONG_PTR)proc);check(SendMessageW(h,WM_APP,0,0)==42);check(GetWindowLongPtrW(h,GWLP_WNDPROC)==(LONG_PTR)subclass);
 SetLastError(0);check(!GetWindowLongA(h,GWLP_WNDPROC)&&GetLastError()==ERROR_INVALID_INDEX);
 HWND child=CreateWindowExW(0,wc.lpszClassName,L"child",WS_CHILD|WS_VISIBLE,2,3,10,20,h,(HMENU)77,0,(void*)0x12345678);check(child&&GetParent(child)==h&&GetDlgItem(h,77)==child);check(SetDlgItemTextW(h,77,L"child text"));
 POINT cp={0,0};check(ClientToScreen(child,&cp)&&cp.x==25&&cp.y==50);
 check(IsWindow(h)&&IsWindowVisible(h)&&!IsIconic(h));check(ShowWindow(h,SW_HIDE));check(!IsWindowVisible(h)&&!IsWindowVisible(child));check(!ShowWindow(h,SW_SHOW));
 POINT p={1,2};check(ClientToScreen(h,&p)&&p.x==24&&p.y==49);check(ScreenToClient(h,&p)&&p.x==1&&p.y==2);
 RECT r={};check(GetClientRect(h,&r)&&r.right==320&&r.bottom==240);check(GetWindowRect(h,&r)&&r.left==23&&r.top==47&&r.right==343);
 check(SetWindowTextW(h,L"changed"));WCHAR title[16];check(SendMessageW(h,WM_GETTEXT,16,(LPARAM)title)==7&&title[0]==L'c');
 check(MoveWindow(h,3,4,123,234,FALSE));check(GetWindowRect(h,&r)&&r.left==3&&r.bottom==238);
 int n=0;check(EnumWindows(count,(LPARAM)&n)&&n>=1);
 RECT x={1,2,5,6},y={};check(CopyRect(&y,&x)&&y.left==1);check(PtInRect(&x,POINT{1,2})&&!PtInRect(&x,POINT{5,6}));check(OffsetRect(&y,2,3)&&y.left==3&&y.top==5);
 check(InflateRect(&y,1,1)&&y.left==2);check(UnionRect(&r,&x,&y)&&r.left==1&&r.bottom==10);
 check(DestroyWindow(h)&&destroyed==2&&!IsWindow(child));check(!IsWindow(h));SetLastError(0);check(!GetWindowLongPtrW(h,GWLP_USERDATA)&&GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
 check(UnregisterClassW(wc.lpszClassName,0));check(!CreateWindowExW(0,MAKEINTATOM(a),0,0,0,0,1,1,0,0,0,0));
 wc.lpszClassName=L"U32cReject";wc.lpfnWndProc=reject;check(RegisterClassExW(&wc)!=0);check(!CreateWindowExW(0,wc.lpszClassName,0,0,0,0,1,1,0,0,0,0));check(rejectedDestroyed==1&&rejectedCreated==0);check(UnregisterClassW(wc.lpszClassName,0));
 HMONITOR monitor=MonitorFromWindow(h,MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi={sizeof(mi)};check(GetMonitorInfoW(monitor,&mi)&&mi.rcMonitor.right==1920);check(GetSystemMetrics(SM_CMONITORS)==1);check(IsWindow(GetDesktopWindow()));
 if(failures){OutputDebugStringA("U32C TEST FAILED");return 1;}OutputDebugStringA("U32C TEST PASS class/long/property/geometry/lifecycle");return 42;
}
