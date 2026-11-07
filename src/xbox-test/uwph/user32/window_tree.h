#pragma once
// Included inside core.cpp after window_extras.h.
#ifndef GW_HWNDFIRST   // hidden from the UWP partition; documented values
#define GW_HWNDFIRST 0
#define GW_HWNDLAST 1
#define GW_HWNDNEXT 2
#define GW_HWNDPREV 3
#define GW_OWNER 4
#define GW_CHILD 5
#define GW_ENABLEDPOPUP 6
#endif
// Z-order: Windows places a new window on top of its siblings and this
// backend never reorders, so siblings run newest first. Top-level windows are
// the desktop's children; message-only windows have no siblings.
static bool childWindow(const Window* w){return (w->style&WS_CHILD)&&w->parent;}
static std::vector<Window*> zOrder(const Window* parent){
 std::vector<Window*> list;
 for(auto it=windows.rbegin();it!=windows.rend();++it){
  Window* w=it->get();
  if(!w->alive||w==desktop||w->messageOnly)continue;
  if(parent==desktop?!childWindow(w):(childWindow(w)&&(const Window*)w->parent==parent))list.push_back(w);
 }
 return list;
}
extern "C" HWND WINAPI c_GetWindow(HWND h,UINT cmd){
 std::lock_guard<std::recursive_mutex> l(mutex);
 c_GetDesktopWindow();
 auto w=window(h);if(!w)return nullptr;
 if(cmd==GW_CHILD){auto list=zOrder(w);return list.empty()?nullptr:(HWND)list.front();}
 if(cmd==GW_OWNER)return childWindow(w)?nullptr:w->parent;
 if(cmd==GW_ENABLEDPOPUP)return nullptr;
 if(cmd>GW_HWNDPREV){SetLastError(1414 /*ERROR_INVALID_GW_COMMAND*/);return nullptr;}
 if(w==desktop)return nullptr;
 auto list=zOrder(childWindow(w)?(Window*)w->parent:desktop);
 if(list.empty())return nullptr;
 if(cmd==GW_HWNDFIRST)return (HWND)list.front();
 if(cmd==GW_HWNDLAST)return (HWND)list.back();
 for(size_t i=0;i<list.size();++i)if(list[i]==w){
  if(cmd==GW_HWNDNEXT)return i+1<list.size()?(HWND)list[i+1]:nullptr;
  return i?(HWND)list[i-1]:nullptr;
 }
 return nullptr;
}
extern "C" HWND WINAPI c_GetTopWindow(HWND h){
 std::lock_guard<std::recursive_mutex> l(mutex);
 Window* parent=h?window(h):(c_GetDesktopWindow(),desktop);
 if(!parent)return nullptr;
 auto list=zOrder(parent);return list.empty()?nullptr:(HWND)list.front();
}
// CloseWindow minimizes a top-level window; it never destroys it (Wine: FALSE for a child).
extern "C" BOOL WINAPI c_CloseWindow(HWND h){
 std::lock_guard<std::recursive_mutex> l(mutex);
 auto w=window(h);if(!w||childWindow(w))return FALSE;
 c_ShowWindow(h,SW_MINIMIZE);return TRUE;
}
extern "C" BOOL WINAPI c_EqualRect(const RECT* a,const RECT* b){
 return a&&b&&a->left==b->left&&a->top==b->top&&a->right==b->right&&a->bottom==b->bottom;
}
// No system sounds play here; the beep request itself succeeds (Wine queues a Beep).
extern "C" BOOL WINAPI c_MessageBeep(UINT){return TRUE;}
extern "C" LPWSTR WINAPI c_CharNextW(LPCWSTR p){return const_cast<LPWSTR>(p&&*p?p+1:p);}
extern "C" LPSTR WINAPI c_CharNextA(LPCSTR p){
 if(!p||!*p)return const_cast<LPSTR>(p);
 return const_cast<LPSTR>(IsDBCSLeadByte(BYTE(*p))&&p[1]?p+2:p+1);
}
// wsprintf: at most 1024 characters including the terminator.
extern "C" int WINAPI c_wvsprintfW(LPWSTR out,LPCWSTR format,va_list args){
 if(!out||!format)return -1;
 int n=_vsnwprintf(out,1023,format,args);
 if(n<0||n>1023)n=1023;
 out[n]=0;return n;
}
extern "C" int __cdecl c_wsprintfW(LPWSTR out,LPCWSTR format,...){
 va_list args;va_start(args,format);int n=c_wvsprintfW(out,format,args);va_end(args);return n;
}
// The template is never loaded, so the ANSI entry has nothing to convert.
extern "C" INT_PTR WINAPI c_DialogBoxIndirectParamA(HINSTANCE i,const void* t,HWND owner,DLGPROC proc,LPARAM p){return c_DialogBoxIndirectParamW(i,t,owner,proc,p);}
// Class fields (GCLP_*/GCL_*) and the class's extra bytes. Setters return the
// previous value; the atom is read-only.
static Class* windowClass(HWND h){auto w=window(h);if(!w)return nullptr;if(!w->cls)SetLastError(ERROR_INVALID_WINDOW_HANDLE);return w->cls;}
static bool classField(Class* c,int index,ULONG_PTR*& field,bool& narrow){
 narrow=false;
 switch(index){
 case GCLP_HICON:field=(ULONG_PTR*)&c->info.icon;return true;
 case GCLP_HICONSM:field=(ULONG_PTR*)&c->info.smallIcon;return true;
 case GCLP_HCURSOR:field=(ULONG_PTR*)&c->info.cursor;return true;
 case GCLP_HBRBACKGROUND:field=(ULONG_PTR*)&c->info.brush;return true;
 case GCLP_WNDPROC:field=(ULONG_PTR*)&c->info.proc;return true;
 case GCLP_HMODULE:field=(ULONG_PTR*)&c->info.instance;return true;
 }
 return false;
}
static ULONG_PTR classLong(HWND h,int index,bool set,ULONG_PTR value){
 std::lock_guard<std::recursive_mutex> l(mutex);
 Class* c=windowClass(h);if(!c)return 0;
 if(index>=0){
  if(size_t(index)+sizeof(ULONG_PTR)>c->extra.size()){SetLastError(ERROR_INVALID_INDEX);return 0;}
  ULONG_PTR old;memcpy(&old,c->extra.data()+index,sizeof old);
  if(set)memcpy(c->extra.data()+index,&value,sizeof value);
  return old;
 }
 ULONG_PTR* field=nullptr;bool narrow;
 if(classField(c,index,field,narrow)){ULONG_PTR old=*field;if(set)*field=value;return old;}
 UINT* small=index==GCL_STYLE?&c->info.style:nullptr;
 if(small){ULONG_PTR old=*small;if(set)*small=UINT(value);return old;}
 if(index==GCL_CBWNDEXTRA&&!set)return ULONG_PTR(c->info.wndExtra);
 if(index==GCL_CBCLSEXTRA&&!set)return ULONG_PTR(c->info.clsExtra);
 if(index==GCW_ATOM&&!set)return c->atom;
 SetLastError(ERROR_INVALID_INDEX);return 0;
}
extern "C" ULONG_PTR WINAPI c_GetClassLongPtrW(HWND h,int index){return classLong(h,index,false,0);}
extern "C" ULONG_PTR WINAPI c_SetClassLongPtrW(HWND h,int index,LONG_PTR value){return classLong(h,index,true,ULONG_PTR(value));}
extern "C" ULONG_PTR WINAPI c_SetClassLongPtrA(HWND h,int index,LONG_PTR value){return classLong(h,index,true,ULONG_PTR(value));}
// A synchronous send to this backend's records (no other process exists to
// time out). HWND_BROADCAST reaches each top-level window.
extern "C" LRESULT WINAPI c_SendMessageTimeoutW(HWND h,UINT m,WPARAM w,LPARAM p,UINT,UINT,PDWORD_PTR result){
 if(h==HWND_BROADCAST){
  std::vector<HWND> targets;
  {std::lock_guard<std::recursive_mutex> l(mutex);c_GetDesktopWindow();for(auto t:zOrder(desktop))targets.push_back((HWND)t);}
  for(auto t:targets)c_SendMessageW(t,m,w,p);
  if(result)*result=0;return TRUE;
 }
 {std::lock_guard<std::recursive_mutex> l(mutex);if(!window(h))return 0;}
 LRESULT r=c_SendMessageW(h,m,w,p);if(result)*result=DWORD_PTR(r);return TRUE;
}
// Play starts no child processes, so no handle names a GUI process with an
// input queue to become idle.
extern "C" DWORD WINAPI c_WaitForInputIdle(HANDLE,DWORD){SetLastError(1471 /*ERROR_NOT_GUI_PROCESS*/);return WAIT_FAILED;}
