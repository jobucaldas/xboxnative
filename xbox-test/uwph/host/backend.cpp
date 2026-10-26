#include <unknwn.h>
#include "backend.h"
#include "server.h"
// All handlers use Windows x64 ABI. Never reuse Wine's sysv dispatcher or GS frame.
static LONG WINAPI dispatch(uint64_t handle, unsigned op, void* args) {
 if (handle != 1 || !args || op >= unsigned(Op::Count)) return LONG(0xc000000d);
 auto& c = *static_cast<Call*>(args); uintptr_t local[6]; memcpy(local,c.a,sizeof(local)); auto* a=local;
 std::shared_ptr<ServerObject> pinned;
 c.result=0; c.error=0;
 SetLastError(0);
 // Pin the object before releasing the table lock; CloseHandle may race safely.
 switch (Op(op)) {
 case Op::Read: case Op::Write: case Op::Seek: case Op::Size: case Op::FlushFile:
 case Op::SetEvent: case Op::ResetEvent: case Op::ReleaseMutex: case Op::ReleaseSemaphore:
 case Op::Wait:
  pinned=server_pin(a[0]);
  if (!pinned) { c.error=ERROR_INVALID_HANDLE; c.result=Op(op)==Op::Wait?WAIT_FAILED:0; return 0; }
  if (!server_accepts(Op(op),pinned->type)) { c.error=ERROR_INVALID_HANDLE; return 0; }
  a[0]=(uintptr_t)pinned->native; break;
 default: break;
 }
 switch (Op(op)) {
 case Op::Alloc:
  // Reject RWX instead of silently changing requested memory semantics.
  if (a[3] & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) { SetLastError(ERROR_ACCESS_DENIED); break; }
  c.result=(uintptr_t)VirtualAllocFromApp((void*)a[0],a[1],DWORD(a[2]),DWORD(a[3])); break;
 case Op::Protect:
  if (a[2] & (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) { SetLastError(ERROR_ACCESS_DENIED); break; }
  c.result=VirtualProtectFromApp((void*)a[0],a[1],DWORD(a[2]),(DWORD*)a[3]); break;
 case Op::Free: c.result=VirtualFree((void*)a[0],a[1],DWORD(a[2])); break;
 case Op::Query: c.result=VirtualQuery((void*)a[0],(MEMORY_BASIC_INFORMATION*)a[1],a[2]); break;
 case Op::Flush: c.result=FlushInstructionCache(GetCurrentProcess(),(void*)a[0],a[1]); break;
 case Op::CreateFile: { CREATEFILE2_EXTENDED_PARAMETERS p{sizeof(p)}; p.dwFileAttributes=FILE_ATTRIBUTE_NORMAL;
 c.result=(uintptr_t)CreateFile2((wchar_t*)a[0],DWORD(a[1]),DWORD(a[2]),DWORD(a[3]),&p); break; }
 case Op::Read: c.result=ReadFile((HANDLE)a[0],(void*)a[1],DWORD(a[2]),(DWORD*)a[3],nullptr); break;
 case Op::Write: c.result=WriteFile((HANDLE)a[0],(void*)a[1],DWORD(a[2]),(DWORD*)a[3],nullptr); break;
 case Op::Seek: { LARGE_INTEGER n; n.QuadPart=a[1]; c.result=SetFilePointerEx((HANDLE)a[0],n,(LARGE_INTEGER*)a[2],DWORD(a[3])); break; }
 case Op::Size: c.result=GetFileSizeEx((HANDLE)a[0],(LARGE_INTEGER*)a[1]); break;
 case Op::FlushFile: c.result=FlushFileBuffers((HANDLE)a[0]); break;
 case Op::DeleteFile: c.result=DeleteFileW((wchar_t*)a[0]); break;
 case Op::MoveFile: c.result=MoveFileExW((wchar_t*)a[0],(wchar_t*)a[1],DWORD(a[2])); break;
 case Op::Attributes: c.result=GetFileAttributesW((wchar_t*)a[0]); break;
 case Op::CreateDir: c.result=CreateDirectoryW((wchar_t*)a[0],nullptr); break;
 case Op::RemoveDir: c.result=RemoveDirectoryW((wchar_t*)a[0]); break;
 case Op::Close: c.result=server_close(a[0]); break;
 case Op::Duplicate: c.result=server_duplicate(a[0]); break;
 case Op::WaitMultiple: c.result=server_wait((const uintptr_t*)a[0],DWORD(a[1]),BOOL(a[2]),DWORD(a[3])); break;
 case Op::Thread: c.result=(uintptr_t)CreateThread(nullptr,0,(LPTHREAD_START_ROUTINE)a[0],(void*)a[1],0,(DWORD*)a[2]); break;
 case Op::ThreadId: c.result=GetCurrentThreadId(); break;
 case Op::ProcessId: c.result=GetCurrentProcessId(); break;
 case Op::Event: c.result=(uintptr_t)CreateEventExW(nullptr,nullptr,DWORD(a[0]),EVENT_ALL_ACCESS); break;
 case Op::SetEvent: c.result=SetEvent((HANDLE)a[0]); break;
 case Op::ResetEvent: c.result=ResetEvent((HANDLE)a[0]); break;
 case Op::Wait: c.result=WaitForSingleObjectEx((HANDLE)a[0],DWORD(a[1]),FALSE); break;
 case Op::Mutex: c.result=(uintptr_t)CreateMutexExW(nullptr,nullptr,0,MUTEX_ALL_ACCESS); break;
 case Op::ReleaseMutex: c.result=ReleaseMutex((HANDLE)a[0]); break;
 case Op::Semaphore: c.result=(uintptr_t)CreateSemaphoreExW(nullptr,LONG(a[0]),LONG(a[1]),nullptr,0,SEMAPHORE_ALL_ACCESS); break;
 case Op::ReleaseSemaphore: c.result=ReleaseSemaphore((HANDLE)a[0],LONG(a[1]),nullptr); break;
 case Op::TlsAlloc: c.result=TlsAlloc(); break;
 case Op::TlsSet: c.result=TlsSetValue(DWORD(a[0]),(void*)a[1]); break;
 case Op::TlsGet: c.result=(uintptr_t)TlsGetValue(DWORD(a[0])); break;
 case Op::TlsFree: c.result=TlsFree(DWORD(a[0])); break;
 case Op::Counter: c.result=QueryPerformanceCounter((LARGE_INTEGER*)a[0]); break;
 case Op::Frequency: c.result=QueryPerformanceFrequency((LARGE_INTEGER*)a[0]); break;
 case Op::Tick: c.result=GetTickCount64(); break;
 case Op::Sleep: Sleep(DWORD(a[0])); c.result=1; break;
 case Op::Process: c.result=0; SetLastError(ERROR_ACCESS_DENIED); break;
 default: return LONG(0xc0000002);
 }
 DWORD error=GetLastError();
 ObjectType type=ObjectType::File; bool created=true;
 switch(Op(op)) {
 case Op::CreateFile: break;
 case Op::Thread: type=ObjectType::Thread; break;
 case Op::Event: type=ObjectType::Event; break;
 case Op::Mutex: type=ObjectType::Mutex; break;
 case Op::Semaphore: type=ObjectType::Semaphore; break;
 default: created=false; break;
 }
 if(created && c.result && c.result!=(uintptr_t)INVALID_HANDLE_VALUE)
  c.result=server_insert((HANDLE)c.result,type);
 c.error=error; return 0;
}
extern "C" { uint64_t __wine_unixlib_handle=1; Dispatch __wine_unix_call_dispatcher=dispatch; }

// Source-derived adapter for Wine 10.0 dlls/ntdll/unixlib.h's eight-entry
// ntdll_unix_funcs table (unix/loader.c:unix_call_funcs). This entry point
// replaces the dispatcher, rather than calling Wine's sysv/GS trampoline.
// Native Nt* syscalls are a separate boundary; they do not use this table.
extern "C" __declspec(dllexport) LONG WINAPI uwph_wine10_unix_dispatcher(uint64_t handle, unsigned op, void* args) {
 if (handle != 10 || !args || op >= 8) return LONG(0xc000000d);
 if (op == 7) { // unix/sync.c:system_time_precise, LONGLONG* result
  FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
  *(LONGLONG*)args = (LONGLONG(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;
  return 0;
 }
 if (op == 2) { // unix/debug.c:unixcall_wine_dbg_write returns byte count
  struct Params { const char* str; unsigned len; };
  const auto& p = *(const Params*)args;
  if ((!p.str && p.len) || p.len > 0x7fffffff) return LONG(0xc000000d);
  unsigned offset=0;
  while (offset<p.len) {
   char buf[512]; unsigned n=p.len-offset; if(n>511) n=511;
   memcpy(buf,p.str+offset,n); buf[n]=0; OutputDebugStringA(buf); offset+=n;
  }
  return LONG(p.len);
 }
 // No ELF loader, unwind emulation, wineserver IPC, handle/fd exchange or
 // subprocess spawning. Unsupported operations must fail visibly.
 return LONG(0xc0000002);
}

// UI-thread-only private CoreWindow bridge. Getter transfers one COM reference.
static IUnknown* swapchain_target;
extern "C" __declspec(dllexport) void uwph_set_swapchain_target(IUnknown* target) {
 if (target) target->AddRef();
 if (swapchain_target) swapchain_target->Release();
 swapchain_target=target;
}
extern "C" __declspec(dllexport) HRESULT uwph_get_swapchain_target(IUnknown** out) {
 if (!out) return E_POINTER;
 *out=swapchain_target;
 if (!*out) return E_UNEXPECTED;
 (*out)->AddRef(); return S_OK;
}
