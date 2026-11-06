#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// The rest of the user32 surface SDL3 (and similar frameworks) import:
// ANSI entry points over the W records, window-state queries, clipboard and
// message bookkeeping, and the parts with no backend here (menus, hooks,
// painting, layered composition) answered as Wine/Windows do when the object
// does not exist. Included inside namespace u32c after message_queue.h.

// ---- ANSI class/window creation over the W records (message payloads are
// identical for these posted/scalar messages; no A/W text thunking exists).
struct ClassExA {UINT size,style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCSTR menu,name;HICON smallIcon;};
static_assert(sizeof(ClassExA)==80,"WNDCLASSEXA layout");
extern "C" ATOM WINAPI c_RegisterClassExA(const ClassExA* p){
 if(!p||p->size!=sizeof(*p)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 auto name=wide(p->name),menu=wide(p->menu);
 ClassEx x{sizeof(x),p->style,p->proc,p->clsExtra,p->wndExtra,p->instance,p->icon,p->cursor,p->brush,
  integer(p->menu)?(LPCWSTR)p->menu:menu.c_str(),integer(p->name)?(LPCWSTR)p->name:name.c_str(),p->smallIcon};
 std::lock_guard<std::recursive_mutex> l(mutex);
 ATOM atom=c_RegisterClassExW(&x);
 if(atom)for(auto& c:classes)if(c->atom==atom&&c->info.instance==p->instance)c->ansi=true;
 return atom;
}
extern "C" BOOL WINAPI c_UnregisterClassA(LPCSTR n,HINSTANCE i){auto w=wide(n);return c_UnregisterClassW(integer(n)?(LPCWSTR)n:w.c_str(),i);}
extern "C" HWND WINAPI c_CreateWindowExA(DWORD ex,LPCSTR n,LPCSTR title,DWORD style,int x,int y,int cx,int cy,HWND parent,HMENU menu,HINSTANCE i,void* param){
 auto cls=wide(n),text=wide(title);
 return c_CreateWindowExW(ex,integer(n)?(LPCWSTR)n:cls.c_str(),title?text.c_str():nullptr,style,x,y,cx,cy,parent,menu,i,param);
}
struct ClassA {UINT style;Proc proc;int clsExtra,wndExtra;HINSTANCE instance;HICON icon;HCURSOR cursor;HBRUSH brush;LPCSTR menu,name;};
static_assert(sizeof(ClassA)==72,"WNDCLASSA layout");
extern "C" ATOM WINAPI c_RegisterClassA(const ClassA* p){
 if(!p){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 ClassExA x{sizeof(x),p->style,p->proc,p->clsExtra,p->wndExtra,p->instance,p->icon,p->cursor,p->brush,p->menu,p->name,nullptr};
 return c_RegisterClassExA(&x);
}
// ANSI messages over the W records. Window text is the one payload converted
// (WM_SETTEXT, WM_GETTEXT); other messages are scalar or carry the same
// structures, as for SendDlgItemMessageA.
static LRESULT ansiText(LRESULT (WINAPI* call)(HWND,UINT,WPARAM,LPARAM),HWND h,UINT m,WPARAM w,LPARAM p){
 if(m==WM_SETTEXT&&p){auto text=wide((LPCSTR)p);return call(h,m,w,(LPARAM)text.c_str());}
 if(m==WM_GETTEXT&&p&&w){
  std::wstring buffer(size_t(w),L'\0');LRESULT n=call(h,m,w,(LPARAM)buffer.data());
  int bytes=WideCharToMultiByte(CP_ACP,0,buffer.c_str(),int(n),(LPSTR)p,int(w)-1,nullptr,nullptr);
  ((LPSTR)p)[bytes]=0;return bytes;
 }
 return call(h,m,w,p);
}
extern "C" LRESULT WINAPI c_DefWindowProcA(HWND h,UINT m,WPARAM w,LPARAM p){return ansiText(c_DefWindowProcW,h,m,w,p);}
// An ANSI window's procedure receives ANSI text unchanged; a Unicode window's
// receives it converted (Windows decides by the window procedure's class).
extern "C" LRESULT WINAPI c_SendMessageA(HWND h,UINT m,WPARAM w,LPARAM p){
 return ansiWindow(h)?sendRaw(h,m,w,p):ansiText(c_SendMessageW,h,m,w,p);
}
extern "C" BOOL WINAPI c_PostMessageA(HWND h,UINT m,WPARAM w,LPARAM p){return c_PostMessageW(h,m,w,p);} // posted payloads are scalar; WM_CHAR > 127 is refused by the W path
extern "C" BOOL WINAPI c_GetKeyboardLayoutNameA(LPSTR name){if(!name)return FALSE;strcpy(name,"00000409");return TRUE;} // fixed en-US layout, as the W form
extern "C" HANDLE WINAPI c_RegisterDeviceNotificationA(HANDLE recipient,void* filter,DWORD flags){return c_RegisterDeviceNotificationW(recipient,filter,flags);} // same validation; the A filter types differ only in name fields
// SendInput: synthesized keyboard input updates the key state table and is
// posted to the focus window as WM_KEYDOWN/WM_KEYUP; mouse moves set the
// cursor (absolute coordinates are 0..65535 over the virtual desktop). Unicode
// keystrokes (no virtual key) and mouse buttons/wheel are not injected, and the
// call stops at the first such entry, returning how many were inserted.
struct KeyInput {WORD vk,scan;DWORD flags,time;ULONG_PTR extra;};
struct MouseInput {LONG dx,dy;DWORD data,flags,time;ULONG_PTR extra;};
struct Input {DWORD type;union {MouseInput mouse;KeyInput key;BYTE raw[32];};};
static_assert(sizeof(Input)==40,"INPUT layout");
extern "C" UINT WINAPI c_SendInput(UINT count,Input* inputs,int size){
 if(size!=int(sizeof(Input))||(count&&!inputs)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 UINT done=0;
 for(;done<count;++done){
  auto& in=inputs[done];
  if(in.type==1){ // INPUT_KEYBOARD
   if((in.key.flags&4)||!in.key.vk||in.key.vk>255){SetLastError(ERROR_NOT_SUPPORTED);break;} // KEYEVENTF_UNICODE
   bool up=(in.key.flags&2)!=0;uwph_u32c_key_event(in.key.vk,!up);
   HWND target=c_GetFocus();
   if(target)c_PostMessageW(target,up?WM_KEYUP:WM_KEYDOWN,in.key.vk,up?LPARAM(0xC0000001):1);
  } else if(in.type==0){ // INPUT_MOUSE
   if(in.mouse.flags&~DWORD(0x8001)){SetLastError(ERROR_NOT_SUPPORTED);break;} // only MOVE | ABSOLUTE
   std::lock_guard<std::recursive_mutex> l(mutex);
   POINT& pos=cursorPosRef();
   if(in.mouse.flags&0x8000)pos={LONG((LONGLONG(in.mouse.dx)*desktopWidth)>>16),LONG((LONGLONG(in.mouse.dy)*desktopHeight)>>16)};
   else{pos.x+=in.mouse.dx;pos.y+=in.mouse.dy;}
   clampToClip(pos);haveCursorPos=true;
  } else {SetLastError(ERROR_INVALID_PARAMETER);break;}
 }
 return done;
}
// IsWindowUnicode: whether the window procedure's class was registered through
// the W API (RegisterClass(Ex)A marks it ANSI).
extern "C" BOOL WINAPI c_IsWindowUnicode(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w&&!(w->cls&&w->cls->ansi);}
// Wine: system DPI awareness, and TRUE even when the awareness was already set.
extern "C" BOOL WINAPI c_SetProcessDPIAware(){c_SetProcessDpiAwarenessInternal(1);return TRUE;}
// The window a GetDC/BeginPaint DC belongs to (NULL for others and the screen DC).
extern "C" HWND WINAPI c_WindowFromDC(HDC dc){std::lock_guard<std::recursive_mutex> l(mutex);for(auto& d:dcs)if((HDC)d.get()==dc)return d->hwnd;return nullptr;}
extern "C" UINT WINAPI c_RegisterWindowMessageW(LPCWSTR s){return c_GlobalAddAtomW(s);}   // same atom table (Wine)
extern "C" UINT WINAPI c_RegisterClipboardFormatW(LPCWSTR s){return c_GlobalAddAtomW(s);}
extern "C" int WINAPI c_GetClipboardFormatNameA(UINT format,LPSTR out,int size){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(!out||size<=0){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 auto a=atoms.find(ATOM(format));if(format<0xc000||a==atoms.end()){SetLastError(ERROR_INVALID_PARAMETER);out[0]=0;return 0;}
 int n=WideCharToMultiByte(CP_ACP,0,a->second.name.c_str(),-1,out,size,nullptr,nullptr);
 if(!n){out[size-1]=0;return size-1;}
 return n-1;
}

// ---- Window state on the records
extern "C" BOOL WINAPI c_IsZoomed(HWND h){return (c_GetWindowLongW(h,GWL_STYLE)&WS_MAXIMIZE)!=0;}
// Returns whether the window was previously disabled.
extern "C" BOOL WINAPI c_EnableWindow(HWND h,BOOL enable){
 std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;
 BOOL was=(w->style&WS_DISABLED)!=0;
 if(enable)w->style&=~WS_DISABLED;else w->style|=WS_DISABLED;
 if(was==!enable)return was;
 w->proc(h,WM_ENABLE,enable,0);return was;
}
extern "C" HWND WINAPI c_SetActiveWindow(HWND h){
 std::lock_guard<std::recursive_mutex> l(mutex);
 HWND old=activeWindow&&window(activeWindow)?activeWindow:nullptr;
 if(h){auto w=window(h);if(!w)return nullptr;if(w->thread!=GetCurrentThreadId()){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}}
 activeWindow=h;SetLastError(ERROR_SUCCESS);return old;
}
extern "C" HWND WINAPI c_SetParent(HWND h,HWND parent){
 std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w||w==desktop)return nullptr;
 if(parent==HWND_MESSAGE){HWND old=w->parent;w->parent=nullptr;w->messageOnly=true;return old?old:c_GetDesktopWindow();}
 if(parent&&parent!=(HWND)desktop&&!window(parent))return nullptr;
 for(HWND p=parent;p&&p!=(HWND)desktop;){if(p==h){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}auto r=window(p);if(!r)break;p=r->parent;}
 HWND old=w->parent?w->parent:c_GetDesktopWindow();
 w->parent=parent==(HWND)desktop?nullptr:parent;w->messageOnly=false;return old;
}
extern "C" int WINAPI c_GetWindowTextLengthW(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);return w?int(w->title.size()):0;}
extern "C" int WINAPI c_GetWindowTextW(HWND h,LPWSTR out,int size){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(!out||size<=0)return 0;
 auto w=window(h);if(!w){out[0]=0;return 0;}
 int n=std::min(size-1,int(w->title.size()));memcpy(out,w->title.data(),size_t(n)*2);out[n]=0;return n;
}
// No compositor applies per-window alpha/colour keys or shapes here; the
// calls are accepted for the window kinds Windows accepts them for.
extern "C" BOOL WINAPI c_SetLayeredWindowAttributes(HWND h,COLORREF,BYTE,DWORD){
 std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return FALSE;
 if(!(w->ex&WS_EX_LAYERED)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 return TRUE;
}
extern "C" int WINAPI c_SetWindowRgn(HWND h,HRGN,BOOL){std::lock_guard<std::recursive_mutex> l(mutex);return window(h)?1:0;}
// Returns whether the window was active before the call; nothing flashes.
extern "C" BOOL WINAPI c_FlashWindowEx(const void* info){
 struct Flash{UINT size;HWND hwnd;DWORD flags;UINT count;DWORD timeout;};
 auto f=(const Flash*)info;if(!f||f->size!=sizeof(Flash)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 std::lock_guard<std::recursive_mutex> l(mutex);if(!window(f->hwnd))return FALSE;
 return activeWindow==f->hwnd;
}
// One message queue state is shared by every guest thread here.
extern "C" BOOL WINAPI c_AttachThreadInput(DWORD from,DWORD to,BOOL){
 if(from==to){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 return TRUE;
}

// ---- Geometry
extern "C" BOOL WINAPI c_SetRectEmpty(RECT* r){if(!r)return FALSE;*r={};return TRUE;}
extern "C" BOOL WINAPI c_IntersectRect(RECT* d,const RECT* a,const RECT* b){
 if(!d||!a||!b)return FALSE;
 if(c_IsRectEmpty(a)||c_IsRectEmpty(b)||a->left>=b->right||b->left>=a->right||a->top>=b->bottom||b->top>=a->bottom){*d={};return FALSE;}
 *d={std::max(a->left,b->left),std::max(a->top,b->top),std::min(a->right,b->right),std::min(a->bottom,b->bottom)};return TRUE;
}
extern "C" HMONITOR WINAPI c_MonitorFromPoint(POINT p,DWORD flags){
 std::lock_guard<std::recursive_mutex> l(mutex);
 bool hit=p.x>=0&&p.y>=0&&p.x<desktopWidth&&p.y<desktopHeight;
 return hit||(flags&3)?(HMONITOR)&monitorToken:nullptr;
}
extern "C" BOOL WINAPI c_GetClipCursor(RECT* r){
 std::lock_guard<std::recursive_mutex> l(mutex);if(!r)return FALSE;
 *r=hasClip?clipRectV:RECT{0,0,desktopWidth,desktopHeight};return TRUE;
}
// The one mode of the record display: anything else is not a valid mode.
extern "C" LONG WINAPI c_ChangeDisplaySettingsExW(LPCWSTR device,DEVMODEW* mode,HWND,DWORD,void*){
 if(device&&wcscmp(device,L"\\\\.\\DISPLAY1"))return DISP_CHANGE_BADPARAM;
 if(!mode)return DISP_CHANGE_SUCCESSFUL;   // restore the registry mode: already current
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(((mode->dmFields&DM_PELSWIDTH)&&LONG(mode->dmPelsWidth)!=desktopWidth)||((mode->dmFields&DM_PELSHEIGHT)&&LONG(mode->dmPelsHeight)!=desktopHeight)||
    ((mode->dmFields&DM_BITSPERPEL)&&mode->dmBitsPerPel!=32)||((mode->dmFields&DM_DISPLAYFREQUENCY)&&mode->dmDisplayFrequency>1&&mode->dmDisplayFrequency!=60))
  return DISP_CHANGE_BADMODE;
 return DISP_CHANGE_SUCCESSFUL;
}
extern "C" BOOL WINAPI c_EnumDisplayDevicesW(LPCWSTR device,DWORD num,DISPLAY_DEVICEW* dd,DWORD flags){
 if(!dd)return FALSE;
 std::string narrow;if(device){int n=WideCharToMultiByte(CP_ACP,0,device,-1,nullptr,0,nullptr,nullptr);narrow.resize(n?n:1);if(n)WideCharToMultiByte(CP_ACP,0,device,-1,&narrow[0],n,nullptr,nullptr);narrow.resize(n?n-1:0);}
 DISPLAY_DEVICEA a{};a.cb=sizeof(a);
 if(!c_EnumDisplayDevicesA(device?narrow.c_str():nullptr,num,&a,flags))return FALSE;
 auto copy=[](WCHAR* d,const CHAR* s,int n){MultiByteToWideChar(CP_ACP,0,s,-1,d,n);};
 copy(dd->DeviceName,a.DeviceName,32);copy(dd->DeviceString,a.DeviceString,128);dd->StateFlags=a.StateFlags;
 copy(dd->DeviceID,a.DeviceID,128);copy(dd->DeviceKey,a.DeviceKey,128);return TRUE;
}
extern "C" BOOL WINAPI c_SystemParametersInfoA(UINT action,UINT param,void* pv,UINT ini){return c_SystemParametersInfoW(action,param,pv,ini);}   // non-string actions only
extern "C" UINT WINAPI c_GetRawInputDeviceInfoA(HANDLE device,UINT command,LPVOID data,PUINT size){return c_GetRawInputDeviceInfoW(device,command,data,size);}
extern "C" HCURSOR WINAPI c_LoadCursorW(HINSTANCE inst,LPCWSTR name){return (HCURSOR)c_LoadImageW(inst,name,IMAGE_CURSOR,0,0,LR_DEFAULTSIZE|LR_SHARED);}
extern "C" HICON WINAPI c_CreateIconFromResource(BYTE* bits,DWORD size,BOOL icon,DWORD version){return c_CreateIconFromResourceEx(bits,size,icon,version,0,0,0);}
// Icons from caller bitmaps need GDI bitmap contents this user32 cannot read.
extern "C" HICON WINAPI c_CreateIconIndirect(void*){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}

// ---- Messages: time/position of the last message retrieved on this thread
extern "C" DWORD WINAPI c_GetMessageTime(){return lastMessageTime;}
extern "C" DWORD WINAPI c_GetMessagePos(){return lastMessagePos;}
extern "C" DWORD WINAPI c_GetQueueStatus(UINT flags){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto& queue=messageQueues[GetCurrentThreadId()];
 DWORD bits=queue.empty()?0:(QS_POSTMESSAGE|QS_ALLPOSTMESSAGE);
 return MAKELONG(bits&flags,bits&flags);
}
// No hooks are ever called here: refuse installation instead of accepting
// a hook that would silently never run.
extern "C" HHOOK WINAPI c_SetWindowsHookExW(int,void*,HINSTANCE,DWORD){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
extern "C" BOOL WINAPI c_UnhookWindowsHookEx(HHOOK){SetLastError(ERROR_INVALID_HOOK_HANDLE);return FALSE;}
extern "C" LRESULT WINAPI c_CallNextHookEx(HHOOK,int,WPARAM,LPARAM){return 0;}

// ---- Painting: record windows have no surface or invalid region.
struct PaintStruct{HDC hdc;BOOL erase;RECT paint;BOOL restore,incUpdate;BYTE reserved[32];};
static_assert(sizeof(PaintStruct)==72,"PAINTSTRUCT layout");
extern "C" HDC WINAPI c_BeginPaint(HWND h,PaintStruct* ps){
 if(!ps){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
 std::lock_guard<std::recursive_mutex> l(mutex);auto w=window(h);if(!w)return nullptr;
 *ps={};ps->hdc=c_GetDC(h);ps->paint={0,0,w->cx,w->cy};return ps->hdc;
}
extern "C" BOOL WINAPI c_EndPaint(HWND h,const PaintStruct* ps){if(ps&&ps->hdc)c_ReleaseDC(h,ps->hdc);return TRUE;}
extern "C" BOOL WINAPI c_GetUpdateRect(HWND h,RECT* r,BOOL){std::lock_guard<std::recursive_mutex> l(mutex);if(!window(h))return FALSE;if(r)*r={};return FALSE;}
extern "C" int WINAPI c_FillRect(HDC,const RECT*,HBRUSH){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
extern "C" int WINAPI c_DrawTextW(HDC,LPCWSTR,int,RECT*,UINT){SetLastError(ERROR_NOT_SUPPORTED);return 0;}

// ---- Menus: no window here has a menu, so no menu handle is ever valid.
extern "C" HMENU WINAPI c_GetMenu(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);window(h);return nullptr;}
extern "C" HMENU WINAPI c_CreatePopupMenu(){SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
static void noMenu(){SetLastError(ERROR_INVALID_MENU_HANDLE);}
extern "C" BOOL WINAPI c_DestroyMenu(HMENU){noMenu();return FALSE;}
extern "C" BOOL WINAPI c_DeleteMenu(HMENU,UINT,UINT){noMenu();return FALSE;}
extern "C" BOOL WINAPI c_InsertMenuW(HMENU,UINT,UINT,UINT_PTR,LPCWSTR){noMenu();return FALSE;}
extern "C" DWORD WINAPI c_CheckMenuItem(HMENU,UINT,UINT){noMenu();return DWORD(-1);}
extern "C" BOOL WINAPI c_EnableMenuItem(HMENU,UINT,UINT){noMenu();return -1;}
extern "C" BOOL WINAPI c_GetMenuItemInfoW(HMENU,UINT,BOOL,void*){noMenu();return FALSE;}
extern "C" BOOL WINAPI c_SetMenuItemInfoW(HMENU,UINT,BOOL,const void*){noMenu();return FALSE;}
extern "C" BOOL WINAPI c_TrackPopupMenu(HMENU,UINT,int,int,int,HWND,const RECT*){noMenu();return FALSE;}
extern "C" INT_PTR WINAPI c_DialogBoxIndirectParamW(HINSTANCE i,const void*,HWND owner,DLGPROC proc,LPARAM p){return c_DialogBoxParamW(i,nullptr,owner,proc,p);}

// ---- Clipboard enumeration and change counter
extern "C" UINT WINAPI c_EnumClipboardFormats(UINT format){
 std::lock_guard<std::recursive_mutex> l(mutex);
 if(!clipboardOpen){SetLastError(ERROR_CLIPBOARD_NOT_OPEN);return 0;}
 auto it=format?clipboardData.upper_bound(format):clipboardData.begin();
 SetLastError(ERROR_SUCCESS);return it==clipboardData.end()?0:it->first;
}
extern "C" DWORD WINAPI c_GetClipboardSequenceNumber(){std::lock_guard<std::recursive_mutex> l(mutex);return clipboardSequence;}

// ---- Message boxes: shown by the host (the launcher) when it registers a
// presenter, e.g. as a full-screen dialog; otherwise none can be displayed.
using MessageBoxPresenter=int(WINAPI*)(LPCWSTR text,LPCWSTR caption,UINT type);
static MessageBoxPresenter messageBoxPresenter=nullptr;
extern "C" void WINAPI uwph_u32c_set_message_box(MessageBoxPresenter p){messageBoxPresenter=p;}
extern "C" int WINAPI c_MessageBoxW(HWND,LPCWSTR text,LPCWSTR caption,UINT type){
 if(!messageBoxPresenter){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
 return messageBoxPresenter(text?text:L"",caption?caption:L"Error",type);
}
extern "C" int WINAPI c_MessageBoxA(HWND h,LPCSTR text,LPCSTR caption,UINT type){
 auto t=wide(text),c=wide(caption);return c_MessageBoxW(h,text?t.c_str():nullptr,caption?c.c_str():nullptr,type);
}
