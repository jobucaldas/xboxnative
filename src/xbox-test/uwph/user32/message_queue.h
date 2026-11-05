#pragma once
// Guest queues share the record registry's lock and window ownership. Host
// user32 never sees these HWNDs. Messages contain no marshalled host pointers.
struct GuestMessage { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
static_assert(sizeof(GuestMessage)==48,"Win64 MSG layout");
static std::map<DWORD,std::deque<GuestMessage>> messageQueues;
// Per-thread "new posted message" wake bit (manual-reset event): set by every
// post, cleared whenever that thread checks its queue, as Windows' QS bits.
// Events live for the process, like the queues. Callers hold the mutex.
static std::map<DWORD,HANDLE> queueWakes;
static HANDLE queueWake(DWORD thread) {
 auto& wake=queueWakes[thread];
 if(!wake)wake=CreateEventExW(nullptr,nullptr,CREATE_EVENT_MANUAL_RESET,EVENT_ALL_ACCESS);
 return wake;
}
static bool postGuest(DWORD thread,HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
 auto& queue=messageQueues[thread];
 if(queue.size()>=10000){SetLastError(ERROR_NOT_ENOUGH_QUOTA);return false;}
 queue.push_back({hwnd,message,wp,lp,GetTickCount(),{0,0}});
 if(auto wake=queueWake(thread))SetEvent(wake);
 return true;
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
 // Checking the queue consumes the wake bit; a later post sets it again.
 if(auto wake=queueWake(GetCurrentThreadId()))ResetEvent(wake);
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
// GetMessage blocks on this thread's wake bit. Only posted messages exist here
// (cross-thread sends fail, no input/paint source), so posting is the one
// wake-up. Returns 0 for WM_QUIT, -1 for an invalid window filter.
extern "C" BOOL WINAPI c_GetMessageW(GuestMessage* out,HWND hwnd,UINT first,UINT last) {
 if(!out){SetLastError(ERROR_INVALID_PARAMETER);return -1;}
 for(;;) {
  HANDLE wake;
  {
   std::lock_guard<std::recursive_mutex> lock(mutex);
   if(hwnd&&hwnd!=(HWND)(LONG_PTR)-1&&!window(hwnd)){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return -1;}
   if(c_PeekMessageW(out,hwnd,first,last,PM_REMOVE))return out->message!=WM_QUIT;
   wake=queueWake(GetCurrentThreadId());
  }
  if(!wake){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return -1;}
  WaitForSingleObjectEx(wake,INFINITE,FALSE);
 }
}
// Waits for the caller's handles or a newly posted message (the only queue
// event source here). MWMO_WAITALL's "all handles and input" is not provided.
extern "C" DWORD WINAPI c_MsgWaitForMultipleObjectsEx(DWORD count,const HANDLE* handles,DWORD ms,DWORD mask,DWORD flags) {
 if(count>MAXIMUM_WAIT_OBJECTS-1||(count&&!handles)){SetLastError(ERROR_INVALID_PARAMETER);return WAIT_FAILED;}
 if(flags&MWMO_WAITALL){SetLastError(ERROR_NOT_SUPPORTED);return WAIT_FAILED;}
 HANDLE all[MAXIMUM_WAIT_OBJECTS];
 for(DWORD i=0;i<count;++i)all[i]=handles[i];
 DWORD total=count;
 if(mask&(QS_POSTMESSAGE|QS_ALLPOSTMESSAGE)) {
  std::lock_guard<std::recursive_mutex> lock(mutex);
  auto& queue=messageQueues[GetCurrentThreadId()];
  // MWMO_INPUTAVAILABLE also reports messages already seen but not removed.
  if((flags&MWMO_INPUTAVAILABLE)&&!queue.empty())return WAIT_OBJECT_0+count;
  auto wake=queueWake(GetCurrentThreadId());
  if(!wake){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return WAIT_FAILED;}
  all[total++]=wake;
 }
 BOOL alertable=(flags&MWMO_ALERTABLE)?TRUE:FALSE;
 if(!total)return SleepEx(ms,alertable)==WAIT_IO_COMPLETION?WAIT_IO_COMPLETION:WAIT_TIMEOUT;
 return WaitForMultipleObjectsEx(total,all,FALSE,ms,alertable);
}
extern "C" DWORD WINAPI c_MsgWaitForMultipleObjects(DWORD count,const HANDLE* handles,BOOL waitAll,DWORD ms,DWORD mask) {
 return c_MsgWaitForMultipleObjectsEx(count,handles,ms,mask,waitAll?MWMO_WAITALL:0);
}
// No input source attaches extra information to messages (Wine's default 0).
extern "C" LPARAM WINAPI c_GetMessageExtraInfo() { return 0; }
// Wine 10 NtUserTranslateMessage: key-down messages post WM_CHAR/WM_SYSCHAR
// for each character ToUnicode produces from the current key state; key-up
// messages report TRUE without posting; anything else is not translated.
extern "C" BOOL WINAPI c_TranslateMessage(const GuestMessage* message) {
 if(!message)return FALSE;
 switch(message->message) {
 case WM_KEYUP: case WM_SYSKEYUP: return TRUE;
 case WM_KEYDOWN: case WM_SYSKEYDOWN: break;
 default: return FALSE;
 }
 std::lock_guard<std::recursive_mutex> lock(mutex);
 BYTE state[256];memcpy(state,keyDown,sizeof state);
 WCHAR chars[2];
 int n=c_ToUnicode(UINT(message->wParam),HIWORD(message->lParam),state,chars,2,0);
 DWORD thread=GetCurrentThreadId();
 if(message->hwnd){auto record=window(message->hwnd);if(!record)return TRUE;thread=record->thread;}
 UINT kind=message->message==WM_KEYDOWN?WM_CHAR:WM_SYSCHAR;
 for(int i=0;i<n;++i)postGuest(thread,message->hwnd,kind,chars[i],message->lParam);
 return TRUE;
}
