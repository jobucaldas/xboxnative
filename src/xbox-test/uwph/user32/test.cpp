// Own trusted regression PE. Game images remain non-executing.
#include <windows.h>
static const char* volatile anchor="U32C mapped regression PE";
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_add(LPCWSTR);
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_find(LPCWSTR);
extern "C" __declspec(dllimport) ATOM WINAPI uwph_atom_delete(ATOM);
extern "C" __declspec(dllimport) void WINAPI uwph_u32c_key_event(int,BOOL);
extern "C" __declspec(dllimport) void WINAPI uwph_u32c_input_set_fallback(HWND);
extern "C" __declspec(dllimport) void WINAPI uwph_u32c_input_release_all();
extern "C" __declspec(dllimport) BOOL WINAPI uwph_u32c_input_key(UINT,UINT,BOOL,BOOL,UINT);
static bool streq(const char* a,const char* b){while(*a&&*b){if(*a++!=*b++)return false;}return *a==*b;}
static bool wstreq(const wchar_t* a,const wchar_t* b){while(*a&&*b){if(*a++!=*b++)return false;}return *a==*b;}
static int checks,failures,created,destroyed;
static void check_result(bool ok,const char* expression){++checks;if(!ok){++failures;OutputDebugStringA(expression);}}
#define check(...) check_result((__VA_ARGS__),"U32C CHECK FAIL " #__VA_ARGS__)
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM p){
 if(m==WM_NCCREATE){auto c=(CREATESTRUCTW*)p;check(c->lpCreateParams==(void*)0x12345678);SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)0x1234567887654321ULL);++created;}
 if(m==WM_NCDESTROY)++destroyed;
 return DefWindowProcW(h,m,w,p);
}static LRESULT CALLBACK ansiProc(HWND h,UINT m,WPARAM w,LPARAM p){return DefWindowProcA(h,m,w,p);}

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
 // item 44: input/cursor/focus
 WNDCLASSEXW wc2={};wc2.cbSize=sizeof(wc2);wc2.lpfnWndProc=proc;wc2.lpszClassName=L"U32cItem44";
 check(RegisterClassExW(&wc2)!=0);HWND h2=CreateWindowExW(0,wc2.lpszClassName,L"focus",WS_VISIBLE,0,0,10,10,0,0,0,(void*)0x12345678);check(h2!=0);
 check(!GetFocus());check(SetFocus(h2)==0&&GetFocus()==h2);check(SetFocus(0)==h2&&!GetFocus());
 check(!GetForegroundWindow()&&!GetActiveWindow());check(SetForegroundWindow(h2)&&GetForegroundWindow()==h2&&GetActiveWindow()==h2);
 check(!SetCapture(0));check(SetCapture(h2)==0);check(ReleaseCapture());
 check(!(GetAsyncKeyState('A')&0x8000));uwph_u32c_key_event('A',TRUE);
 {SHORT s=GetAsyncKeyState('A');check((s&0x8000)&&(s&1));}check(!(GetAsyncKeyState('A')&1));check(GetKeyState('A')&0x8000);
 uwph_u32c_key_event('A',FALSE);check(!(GetAsyncKeyState('A')&0x8000));check(!(GetKeyState('A')&0x8000));
 // SendInput: keys reach the key table; absolute mouse moves map 0..65535 to the desktop; Unicode keystrokes are refused.
 {INPUT in[2]={};in[0].type=INPUT_KEYBOARD;in[0].ki.wVk='B';in[1].type=INPUT_MOUSE;in[1].mi.dx=32768;in[1].mi.dy=32768;in[1].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
  check(SendInput(2,in,sizeof(INPUT))==2&&(GetAsyncKeyState('B')&0x8000));
  POINT pt;check(GetCursorPos(&pt)&&pt.x==GetSystemMetrics(SM_CXSCREEN)/2&&pt.y==GetSystemMetrics(SM_CYSCREEN)/2);
  in[0].ki.dwFlags=KEYEVENTF_KEYUP;check(SendInput(1,in,sizeof(INPUT))==1&&!(GetAsyncKeyState('B')&0x8000));
  in[0].ki.dwFlags=KEYEVENTF_UNICODE;in[0].ki.wVk=0;in[0].ki.wScan='x';SetLastError(0);check(SendInput(1,in,sizeof(INPUT))==0&&GetLastError()==ERROR_NOT_SUPPORTED);
  SetLastError(0);check(SendInput(1,in,sizeof(INPUT)-1)==0&&GetLastError()==ERROR_INVALID_PARAMETER);}
 {char layout[KL_NAMELENGTH]={};check(GetKeyboardLayoutNameA(layout)&&!lstrcmpA(layout,"00000409"));}
 // SendInput and the launcher feed are one path: same target (focus, then
 // foreground, then the presenting window), lParams, system keys, sides, mirror.
 {WNDCLASSW fc={};fc.lpfnWndProc=DefWindowProcW;fc.lpszClassName=L"u32cfeed";check(RegisterClassW(&fc));
  HWND fw=CreateWindowExW(0,L"u32cfeed",L"",0,0,0,640,360,0,0,0,0);check(fw);SetFocus(0);check(SetForegroundWindow(fw));
  uwph_u32c_input_set_fallback(fw);MSG fm;while(PeekMessageW(&fm,fw,0,0,PM_REMOVE)){}
  INPUT k={};k.type=INPUT_KEYBOARD;k.ki.wVk='B';check(SendInput(1,&k,sizeof k)==1);
  check(PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_KEYDOWN&&fm.wParam=='B'&&fm.lParam==LPARAM(1|(0x30<<16)));
  k.ki.dwFlags=KEYEVENTF_KEYUP;check(SendInput(1,&k,sizeof k)==1&&PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_KEYUP&&(fm.lParam&0xC0000000)==0xC0000000);
  INPUT alt[2]={};alt[0].type=alt[1].type=INPUT_KEYBOARD;alt[0].ki.wVk=VK_MENU;alt[1].ki.wVk='F';check(SendInput(2,alt,sizeof(INPUT))==2);
  check((GetKeyState(VK_LMENU)&0x8000)&&(GetKeyState(VK_MENU)&0x8000));
  check(PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_SYSKEYDOWN&&fm.wParam==VK_MENU);
  check(PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_SYSKEYDOWN&&fm.wParam=='F'&&(fm.lParam&(1<<29)));
  uwph_u32c_input_release_all();check(!(GetAsyncKeyState('F')&0x8000)&&!(GetKeyState(VK_LMENU)&0x8000)); // one mirror
  while(PeekMessageW(&fm,fw,0,0,PM_REMOVE)){}
  INPUT m={};m.type=INPUT_MOUSE;m.mi.dx=m.mi.dy=32768;m.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTDOWN;check(SendInput(1,&m,sizeof m)==1);
  check(PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_LBUTTONDOWN&&(fm.wParam&MK_LBUTTON));
  check(PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_MOUSEMOVE&&(fm.wParam&MK_LBUTTON));
  m.mi.dwFlags=MOUSEEVENTF_LEFTUP;check(SendInput(1,&m,sizeof m)==1&&PeekMessageW(&fm,fw,0,0,PM_REMOVE)&&fm.message==WM_LBUTTONUP);
  check(uwph_u32c_input_key('Q',0x10,FALSE,TRUE,1)&&(GetAsyncKeyState('Q')&0x8000));uwph_u32c_input_release_all();check(!(GetAsyncKeyState('Q')&0x8000));
  uwph_u32c_input_set_fallback(0);while(PeekMessageW(&fm,fw,0,0,PM_REMOVE)){}check(DestroyWindow(fw));}
 // ANSI class and messages over the W records: window text converts, posted messages pass through.
 {WNDCLASSA ac={};ac.lpfnWndProc=ansiProc;ac.lpszClassName="u32cansi";check(RegisterClassA(&ac));
  HWND ah=CreateWindowExA(0,"u32cansi","",0,0,0,10,10,0,0,0,0);check(ah);
  check(SendMessageA(ah,WM_SETTEXT,0,(LPARAM)"ansi text"));char text[32]={};
  check(SendMessageA(ah,WM_GETTEXT,sizeof text,(LPARAM)text)==9&&!lstrcmpA(text,"ansi text"));
  char small[5]={};check(SendMessageA(ah,WM_GETTEXT,sizeof small,(LPARAM)small)==4&&!lstrcmpA(small,"ansi"));
  check(SetWindowTextW(ah,L"wide text"));WCHAR wtext[32]={};check(SendMessageW(ah,WM_GETTEXT,32,(LPARAM)wtext)==9&&!lstrcmpW(wtext,L"wide text"));
  check(SendMessageA(ah,WM_GETTEXT,sizeof text,(LPARAM)text)==9&&!lstrcmpA(text,"wide text"));
  WCHAR wsmall[5]={};check(SendMessageW(ah,WM_GETTEXT,5,(LPARAM)wsmall)==4&&!lstrcmpW(wsmall,L"wide"));
  check(PostMessageA(ah,WM_USER+5,7,9));MSG am;check(PeekMessageA(&am,ah,WM_USER+5,WM_USER+5,PM_REMOVE)&&am.wParam==7&&am.lParam==9);
  check(DestroyWindow(ah));check(UnregisterClassA("u32cansi",0));}
 POINT cp2; check(GetCursorPos(&cp2)&&cp2.x==960&&cp2.y==540);check(SetCursorPos(10,10)&&GetCursorPos(&cp2)&&cp2.x==10&&cp2.y==10);
 RECT clip{0,0,5,5};check(ClipCursor(&clip));check(GetCursorPos(&cp2)&&cp2.x==4&&cp2.y==4);check(ClipCursor(0));
 int shown=ShowCursor(TRUE);check(ShowCursor(FALSE)==shown-1);
 check(MapVirtualKeyW('A',MAPVK_VK_TO_VSC)==0x1E);check(MapVirtualKeyW(0x1E,MAPVK_VSC_TO_VK)=='A');check(MapVirtualKeyW(VK_LEFT,MAPVK_VK_TO_VSC)==0x4B);
 check(MapVirtualKeyA(VK_RETURN,MAPVK_VK_TO_VSC)==0x1C);check(MapVirtualKeyExA(VK_SHIFT,MAPVK_VK_TO_VSC,0)==0x2A);
 WCHAR layoutName[KL_NAMELENGTH];check(GetKeyboardLayout(0)==(HKL)(ULONG_PTR)0x04090409);check(GetKeyboardLayoutNameW(layoutName)&&wstreq(layoutName,L"00000409"));
 int monitors=0;check(EnumDisplayMonitors(0,0,[](HMONITOR,HDC,RECT* r,LPARAM p)->BOOL{++*(int*)p;return r->right==1920;},(LPARAM)&monitors)&&monitors==1);
 DEVMODEW dm={};check(EnumDisplaySettingsW(0,0,&dm)&&dm.dmPelsWidth==1920&&dm.dmPelsHeight==1080);
 DISPLAY_DEVICEA dd={};dd.cb=sizeof(dd);check(EnumDisplayDevicesA(0,0,&dd,0)&&streq(dd.DeviceName,"\\\\.\\DISPLAY1"));check(!EnumDisplayDevicesA(0,1,&dd,0));
 WINDOWPLACEMENT wp={sizeof(wp)};check(GetWindowPlacement(h2,&wp)&&wp.showCmd==SW_SHOWNORMAL&&wp.rcNormalPosition.right==10);
 RAWINPUTDEVICE rid={1,6,0,h2};check(RegisterRawInputDevices(&rid,1,sizeof(rid)));UINT rc=0;check(GetRawInputDeviceList(0,&rc,sizeof(RAWINPUTDEVICELIST))==0&&rc==0);
 // CCD describes the one record display (Unity's display layout needs it).
 UINT32 npath=0,nmode=0;check(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&npath,&nmode)==ERROR_SUCCESS&&npath==1&&nmode==2);
 check(GetDisplayConfigBufferSizes(0,&npath,&nmode)==ERROR_INVALID_PARAMETER);
 DISPLAYCONFIG_PATH_INFO paths[1]={};DISPLAYCONFIG_MODE_INFO modes[2]={};npath=1;nmode=2;
 check(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&npath,paths,&nmode,modes,0)==ERROR_SUCCESS&&npath==1&&modes[0].sourceMode.width==1920);
 check(QueryDisplayConfig(0,0,0,0,0,0)==ERROR_INVALID_PARAMETER);check(DisplayConfigGetDeviceInfo(0)==ERROR_INVALID_PARAMETER);
 // batch 2: clipboard, timers/tracking, dialog/message helpers, metrics, geometry
 check(!CloseClipboard());check(OpenClipboard(h2));check(!OpenClipboard(0)&&GetLastError()==ERROR_ACCESS_DENIED);
 check(!IsClipboardFormatAvailable(CF_TEXT));check(SetClipboardData(CF_TEXT,(HANDLE)0xABCD)==(HANDLE)0xABCD);
 check(IsClipboardFormatAvailable(CF_TEXT));check(GetClipboardData(CF_TEXT)==(HANDLE)0xABCD);
 check(EmptyClipboard());check(!GetClipboardData(CF_TEXT));check(CloseClipboard());check(!CloseClipboard());
 UINT_PTR t1=SetTimer(h2,0,100,0);check(t1!=0);UINT_PTR t2=SetTimer(h2,42,100,0);check(t2==42);
 check(SetTimer(h2,42,200,0)==42);check(KillTimer(h2,42));check(!KillTimer(h2,42));check(KillTimer(h2,t1));
 TRACKMOUSEEVENT tme{sizeof(tme),TME_LEAVE,h2,0};check(TrackMouseEvent(&tme));
 tme.dwFlags=TME_CANCEL;check(TrackMouseEvent(&tme));
 UINT wm=RegisterWindowMessageA("U32cCustomMessage");check(wm>=0xc000);check(RegisterWindowMessageA("u32ccustommessage")==wm);
 HWND item=CreateWindowExW(0,wc2.lpszClassName,L"item",WS_CHILD,0,0,5,5,h2,(HMENU)77,0,(void*)0x12345678);check(item!=0);
 check(SendDlgItemMessageA(h2,77,WM_APP,0,0)==0);check(SetDlgItemTextA(h2,77,"ansi text"));check(DestroyWindow(item));
 DWORD_PTR smResult=0;check(SendMessageTimeoutA(h2,WM_APP+1,5,0,0,0,&smResult));check(smResult==0);
 check(GetCaretBlinkTime()==530);check(GetDoubleClickTime()==500);
 HANDLE desk=GetThreadDesktop(GetCurrentThreadId());check(desk!=0);
 char uoiBuf[32];DWORD uoiNeed=0;check(GetUserObjectInformationA(desk,UOI_NAME,uoiBuf,sizeof(uoiBuf),&uoiNeed)&&streq(uoiBuf,"Default"));
 check(!GetUserObjectInformationA((HANDLE)0x1,UOI_NAME,uoiBuf,sizeof(uoiBuf),&uoiNeed)&&GetLastError()==ERROR_INVALID_HANDLE);
 RECT work{};check(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)&&work.right==1920);
 SetLastError(0);check(!SystemParametersInfoW(9999,0,&work,0)&&GetLastError()==ERROR_NOT_SUPPORTED);
 RECT adj{10,10,100,100};check(AdjustWindowRect(&adj,WS_OVERLAPPEDWINDOW,FALSE)&&adj.left<10&&adj.top<10&&adj.right>100);
 check(SetWindowPos(h2,0,5,6,20,30,0));RECT wr{};check(GetWindowRect(h2,&wr)&&wr.left==5&&wr.top==6&&wr.right==25);
 check(SetWindowPos(h2,0,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_HIDEWINDOW));check(!IsWindowVisible(h2));
 check(ValidateRect(h2,0));check(UpdateWindow(h2));check(!ValidateRect((HWND)0x99999,0));
 check(InvalidateRect(h2,0,TRUE));check(InvalidateRect(0,0,FALSE));SetLastError(0);check(!InvalidateRect((HWND)0x99999,0,TRUE)&&GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
 BYTE keyState[256]={};keyState[VK_SHIFT]=0x80;WCHAR tu[4]={};check(ToUnicode('A',0,keyState,tu,4,0)==1&&tu[0]==L'A');
 keyState[VK_SHIFT]=0;check(ToUnicode('A',0,keyState,tu,4,0)==1&&tu[0]==L'a');check(ToUnicode(VK_F1,0,keyState,tu,4,0)==0);
 // batch 3: GetDC/ReleaseDC, standard cursor/icon loading, CreateIconFromResourceEx+LookupIconIdFromDirectoryEx
 HDC dc=GetDC(h2);check(dc!=0);check(ReleaseDC(h2,dc)==1);check(ReleaseDC(h2,dc)==0);check(GetDC((HWND)0x99999)==0);
 HCURSOR arrow=LoadCursorA(0,MAKEINTRESOURCEA(32512));check(arrow!=0);check(LoadCursorA(0,MAKEINTRESOURCEA(32512))==arrow);
 check(!LoadCursorA((HINSTANCE)0x1234,MAKEINTRESOURCEA(32512))&&GetLastError()==ERROR_RESOURCE_DATA_NOT_FOUND);
 HICON appIcon=LoadIconA(0,MAKEINTRESOURCEA(32512));check(appIcon!=0);check(LoadIconW(0,MAKEINTRESOURCEW(32512))==appIcon);check(appIcon!=(HICON)arrow);
 HANDLE img=LoadImageW(0,MAKEINTRESOURCEW(32513),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE);check(img&&img!=arrow);
 check(!LoadImageW(0,MAKEINTRESOURCEW(1),IMAGE_BITMAP,0,0,0)&&GetLastError()==ERROR_NOT_SUPPORTED);
 {HANDLE icon=LoadImageW(0,MAKEINTRESOURCEW(32512),IMAGE_ICON,0,0,LR_DEFAULTSIZE);check(icon);
  HANDLE copy=CopyImage(icon,IMAGE_ICON,48,48,0);check(copy&&copy!=icon);
  ICONINFO ci{};check(GetIconInfo((HICON)copy,&ci)&&ci.fIcon&&ci.xHotspot==24&&ci.yHotspot==24);
  HANDLE moved=CopyImage(copy,IMAGE_CURSOR,0,0,LR_COPYDELETEORG);check(moved&&moved!=copy);
  SetLastError(0);check(!DestroyIcon((HICON)copy)&&GetLastError()==ERROR_INVALID_CURSOR_HANDLE); // original deleted
  check(DestroyIcon((HICON)moved));check(DestroyIcon((HICON)icon)); // shared system icon survives LR_COPYDELETEORG
  SetLastError(0);check(!CopyImage(icon,IMAGE_BITMAP,0,0,0)&&GetLastError()==ERROR_NOT_SUPPORTED);
  SetLastError(0);check(!CopyImage((HANDLE)0x4242,IMAGE_ICON,0,0,0)&&GetLastError()==ERROR_INVALID_CURSOR_HANDLE);}
 check(!LoadImageW(0,L"file.cur",IMAGE_CURSOR,0,0,LR_LOADFROMFILE)&&GetLastError()==ERROR_FILE_NOT_FOUND);
 check(DestroyIcon(appIcon));check(DestroyIcon(appIcon)); // shared/system icon destroy is always a TRUE no-op.
 check(!DestroyIcon((HICON)0x7777)&&GetLastError()==ERROR_INVALID_CURSOR_HANDLE);
 BITMAPINFOHEADER bmihdr{};bmihdr.biWidth=16;bmihdr.biHeight=32;BYTE iconBits[sizeof(BITMAPINFOHEADER)];memcpy(iconBits,&bmihdr,sizeof(bmihdr));
 HICON custom=CreateIconFromResourceEx(iconBits,sizeof(iconBits),TRUE,0x00030000,0,0,0);check(custom!=0);
 ICONINFO ii{};check(GetIconInfo(custom,&ii)&&ii.fIcon&&ii.xHotspot==8&&ii.yHotspot==8&&!ii.hbmMask&&!ii.hbmColor);
 check(DestroyIcon(custom));check(!DestroyIcon(custom)&&GetLastError()==ERROR_INVALID_CURSOR_HANDLE); // own (non-shared) handle is actually freed.
 check(!CreateIconFromResourceEx(nullptr,0,TRUE,0,0,0,0)&&GetLastError()==ERROR_INVALID_PARAMETER);
 check(!CreateIconFromResourceEx((BYTE*)"RIFFxxxxACONxxxx",16,TRUE,0x00030000,0,0,0)&&GetLastError()==ERROR_NOT_SUPPORTED);
 #pragma pack(push,1)
 struct DirEntry{BYTE w,h,c,r;WORD planes,bits;DWORD bytes;WORD id;};
 struct Dir{WORD reserved,type,count;DirEntry entries[2];};
 #pragma pack(pop)
 Dir dir{0,1,2,{{16,16,0,0,1,32,0,101},{64,64,0,0,1,32,0,102}}};
 check(LookupIconIdFromDirectoryEx((BYTE*)&dir,TRUE,16,16,0)==101);
 check(LookupIconIdFromDirectoryEx((BYTE*)&dir,TRUE,64,64,0)==102);
 #pragma pack(push,1)
 struct CursorEntry{WORD w,h;WORD planes,bits;DWORD bytes;WORD id;}; // CursorDir2's wWidth/wHeight, not the icon BYTE quad.
 struct CDir{WORD reserved,type,count;CursorEntry entries[2];};
 #pragma pack(pop)
 CDir cdir{0,2,2,{{16,16,1,1,0,201},{64,64,1,1,0,202}}};
 check(LookupIconIdFromDirectoryEx((BYTE*)&cdir,FALSE,20,20,0)==201);
 check(LookupIconIdFromDirectoryEx((BYTE*)&cdir,FALSE,100,100,0)==202);
 Dir bad{1,1,1,{{16,16,0,0,1,32,0,1}}};check(LookupIconIdFromDirectoryEx((BYTE*)&bad,TRUE,16,16,0)==0); // idReserved!=0: invalid directory.
 check(DestroyWindow(h2));check(UnregisterClassW(wc2.lpszClassName,0));
 if(failures){OutputDebugStringA("U32C TEST FAILED");return 1;}OutputDebugStringA("U32C TEST PASS class/long/property/geometry/lifecycle");return 42;
}
