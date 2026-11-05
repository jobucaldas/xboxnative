#pragma once
// Guest queues share the record registry's lock and window ownership. Host
// user32 never sees these HWNDs. Messages contain no marshalled host pointers.
struct GuestMessage { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
static_assert(sizeof(GuestMessage)==48,"Win64 MSG layout");
static std::map<DWORD,std::deque<GuestMessage>> messageQueues;
static bool postGuest(DWORD thread,HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
 auto& queue=messageQueues[thread];
 if(queue.size()>=10000){SetLastError(ERROR_NOT_ENOUGH_QUOTA);return false;}
 queue.push_back({hwnd,message,wp,lp,GetTickCount(),{0,0}});return true;
}
extern "C" BOOL WINAPI c_PostMessageW(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(hwnd==HWND_BROADCAST){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
 if((message==WM_CHAR||message==WM_SYSCHAR||message==WM_DEADCHAR||message==WM_SYSDEADCHAR)&&wp>127){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
 DWORD thread=GetCurrentThreadId();
 if(hwnd){auto record=window(hwnd);if(!record)return FALSE;thread=record->thread;}
 // System messages with pointer parameters need sender-side marshalling.
 if(message==WM_SETTEXT||message==WM_GETTEXT||message==WM_COPYDATA){SetLastError(ERROR_MESSAGE_SYNC_ONLY);return FALSE;}
 return postGuest(thread,hwnd,message,wp,lp);
}
extern "C" BOOL WINAPI c_PostThreadMessageW(DWORD thread,UINT message,WPARAM wp,LPARAM lp) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(messageQueues.find(thread)==messageQueues.end()){SetLastError(ERROR_INVALID_THREAD_ID);return FALSE;}
 return postGuest(thread,nullptr,message,wp,lp);
}
extern "C" void WINAPI c_PostQuitMessage(int code) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 auto& queue=messageQueues[GetCurrentThreadId()];
 for(auto& item:queue)if(item.message==WM_QUIT){item.wParam=code;return;}
 postGuest(GetCurrentThreadId(),nullptr,WM_QUIT,code,0);
}
static bool messageWindow(HWND candidate,HWND filter) {
 if(!filter)return true;
 if(filter==(HWND)(LONG_PTR)-1)return !candidate;
 for(HWND h=candidate;h;){if(h==filter)return true;auto record=window(h);if(!record)return false;h=record->parent;}
 return false;
}
extern "C" BOOL WINAPI c_PeekMessageW(GuestMessage* out,HWND hwnd,UINT first,UINT last,UINT flags) {
 std::lock_guard<std::recursive_mutex> lock(mutex);
 auto& queue=messageQueues[GetCurrentThreadId()];
 if(!out){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 if(hwnd&&hwnd!=(HWND)(LONG_PTR)-1&&!window(hwnd))return FALSE;
 // Only posted messages exist here. A caller filtering exclusively for input,
 // paint or sent-message categories must not consume this queue.
 UINT categories=HIWORD(flags);
 if(categories&&!(categories&QS_POSTMESSAGE)&&!(categories&QS_ALLPOSTMESSAGE))return FALSE;
 for(auto it=queue.begin();it!=queue.end();){
  if(it->hwnd&&!window(it->hwnd)){it=queue.erase(it);continue;}
  if((it->message==WM_QUIT||((!first&&!last)||(it->message>=first&&it->message<=last)))&&messageWindow(it->hwnd,hwnd)){
   *out=*it;if(flags&PM_REMOVE)queue.erase(it);return TRUE;
  }
  ++it;
 }
 return FALSE;
}
extern "C" LRESULT WINAPI c_DispatchMessageW(const GuestMessage* message) {
 if(!message){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
 std::lock_guard<std::recursive_mutex> lock(mutex);
 if(message->message==WM_TIMER&&message->lParam){
  using TimerProc=void(CALLBACK*)(HWND,UINT,UINT_PTR,DWORD);
  ((TimerProc)message->lParam)(message->hwnd,WM_TIMER,message->wParam,GetTickCount());return 0;
 }
 if(!message->hwnd)return 0;
 return c_SendMessageW(message->hwnd,message->message,message->wParam,message->lParam);
}
// A messages carry the same scalar payload for posted non-text messages.
// Character/codepage translation and pointer marshalling are not provided.
extern "C" BOOL WINAPI c_PeekMessageA(GuestMessage* out,HWND hwnd,UINT first,UINT last,UINT flags) {
 return c_PeekMessageW(out,hwnd,first,last,flags);
}
