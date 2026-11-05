#pragma once
// Key names and the dialog/drag entry points Unity imports. Record-only
// windows: no dialog manager, resource templates or pointer capture exist.

// US-layout key names (the names Windows' US keyboard layout reports) for the
// scan codes in keymap.h. lParam bits 16-23 scan code, 24 extended, 25 "don't
// care" (left/right Shift/Ctrl/Alt not distinguished). Unknown keys: 0.
static const wchar_t* keyName(WORD vk,bool ext,bool dontCare) {
 switch(vk) {
 case VK_ESCAPE: return L"Esc"; case VK_BACK: return L"Backspace"; case VK_TAB: return L"Tab";
 case VK_RETURN: return ext?L"Num Enter":L"Enter"; case VK_SPACE: return L"Space";
 case VK_CAPITAL: return L"Caps Lock"; case VK_NUMLOCK: return L"Num Lock"; case VK_SCROLL: return L"Scroll Lock";
 case VK_LSHIFT: return L"Shift"; case VK_RSHIFT: return dontCare?L"Shift":L"Right Shift";
 case VK_LCONTROL: return L"Ctrl"; case VK_RCONTROL: return dontCare?L"Ctrl":L"Right Ctrl";
 case VK_LMENU: return L"Alt"; case VK_RMENU: return dontCare?L"Alt":L"Right Alt";
 case VK_UP: return L"Up"; case VK_DOWN: return L"Down"; case VK_LEFT: return L"Left"; case VK_RIGHT: return L"Right";
 case VK_INSERT: return L"Insert"; case VK_DELETE: return L"Delete"; case VK_HOME: return L"Home"; case VK_END: return L"End";
 case VK_PRIOR: return L"Page Up"; case VK_NEXT: return L"Page Down"; case VK_DIVIDE: return L"Num /";
 case VK_MULTIPLY: return L"Num *"; case VK_SUBTRACT: return L"Num -"; case VK_ADD: return L"Num +"; case VK_DECIMAL: return L"Num Del";
 case VK_F1: return L"F1"; case VK_F2: return L"F2"; case VK_F3: return L"F3"; case VK_F4: return L"F4";
 case VK_F5: return L"F5"; case VK_F6: return L"F6"; case VK_F7: return L"F7"; case VK_F8: return L"F8";
 case VK_F9: return L"F9"; case VK_F10: return L"F10"; case VK_F11: return L"F11"; case VK_F12: return L"F12";
 case VK_NUMPAD0: return L"Num 0"; case VK_NUMPAD1: return L"Num 1"; case VK_NUMPAD2: return L"Num 2";
 case VK_NUMPAD3: return L"Num 3"; case VK_NUMPAD4: return L"Num 4"; case VK_NUMPAD5: return L"Num 5";
 case VK_NUMPAD6: return L"Num 6"; case VK_NUMPAD7: return L"Num 7"; case VK_NUMPAD8: return L"Num 8"; case VK_NUMPAD9: return L"Num 9";
 default: return nullptr;
 }
}
extern "C" int WINAPI c_GetKeyNameTextW(LONG lParam,LPWSTR buffer,int size) {
 if(!buffer||size<=0)return 0;
 BYTE scan=BYTE((lParam>>16)&0xFF);bool ext=(lParam>>24)&1,dontCare=(lParam>>25)&1;
 wchar_t printable[2]={0,0};const wchar_t* name=nullptr;
 for(auto& r:u32c_keymap::rows) {
  if(r.scan!=scan||r.ext!=ext)continue;
  name=keyName(r.vk,ext,dontCare);
  if(!name&&r.ch){printable[0]=wchar_t((r.ch>='a'&&r.ch<='z')?r.ch-32:r.ch);name=printable;}
  break;
 }
 if(!name){buffer[0]=0;return 0;}
 int n=0;for(;name[n]&&n<size-1;++n)buffer[n]=name[n];
 buffer[n]=0;return n;
}
// No dialog manager or template loader: the dialog cannot be created. Windows
// returns -1 for a failure other than an invalid owner window.
extern "C" INT_PTR WINAPI c_DialogBoxParamW(HINSTANCE,LPCWSTR,HWND owner,DLGPROC,LPARAM) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(owner&&!window(owner)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return 0;}
 SetLastError(ERROR_NOT_SUPPORTED);return -1;
}
// No window here is a dialog, so none can be ended (Wine: no dialog info).
extern "C" BOOL WINAPI c_EndDialog(HWND,INT_PTR) {
 SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;
}
// No pointer input or capture exists, so a drag can never start.
extern "C" BOOL WINAPI c_DragDetect(HWND,POINT) { return FALSE; }
// Records belong to the creating thread of this process (Wine/Windows: 0 and
// ERROR_INVALID_WINDOW_HANDLE for an unknown window, output left untouched).
extern "C" DWORD WINAPI c_GetWindowThreadProcessId(HWND hwnd,DWORD* process) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 auto record=window(hwnd);
 if(!record){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return 0;}
 if(process)*process=GetCurrentProcessId();
 return record->thread;
}
