#pragma once
#include <map>
#include <memory>
// Process-local server: table entries own references, waits pin objects independently.
// IDs never recycle, so a stale guest handle cannot alias a later allocation.
enum class ObjectType { Event, Mutex, Semaphore, Thread, File };
struct ServerObject {
 HANDLE native; ObjectType type;
 ServerObject(HANDLE h,ObjectType t):native(h),type(t) {}
 ~ServerObject() { CloseHandle(native); }
};
static SRWLOCK server_lock=SRWLOCK_INIT;
static std::map<uintptr_t,std::shared_ptr<ServerObject>> server_table;
static uintptr_t server_next=0x10000;
static std::shared_ptr<ServerObject> server_pin(uintptr_t id) {
 AcquireSRWLockShared(&server_lock);
 auto it=server_table.find(id);
 auto obj=it==server_table.end()?std::shared_ptr<ServerObject>{}:it->second;
 ReleaseSRWLockShared(&server_lock); return obj;
}
static uintptr_t server_add(std::shared_ptr<ServerObject> obj) {
 AcquireSRWLockExclusive(&server_lock);
 uintptr_t id=server_next++; server_table.emplace(id,std::move(obj));
 ReleaseSRWLockExclusive(&server_lock); return id;
}
static uintptr_t server_insert(HANDLE h,ObjectType t) { return server_add(std::make_shared<ServerObject>(h,t)); }
static BOOL server_close(uintptr_t id) {
 std::shared_ptr<ServerObject> obj;
 AcquireSRWLockExclusive(&server_lock);
 auto it=server_table.find(id);
 if(it!=server_table.end()) { obj=std::move(it->second); server_table.erase(it); }
 ReleaseSRWLockExclusive(&server_lock);
 if(!obj) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
 return TRUE;
}
// Private ABI DuplicateHandle: same process, same access, non-inheritable.
static uintptr_t server_duplicate(uintptr_t id) {
 auto obj=server_pin(id);
 if(!obj) { SetLastError(ERROR_INVALID_HANDLE); return 0; }
 return server_add(std::move(obj));
}
static bool server_accepts(Op op,ObjectType type) {
 switch(op) {
 case Op::Read: case Op::Write: case Op::Seek: case Op::Size: case Op::FlushFile: return type==ObjectType::File;
 case Op::SetEvent: case Op::ResetEvent: return type==ObjectType::Event;
 case Op::ReleaseMutex: return type==ObjectType::Mutex;
 case Op::ReleaseSemaphore: return type==ObjectType::Semaphore;
 default: return true;
 }
}
static DWORD server_wait(const uintptr_t* ids,DWORD count,BOOL all,DWORD timeout) {
 if(!ids || !count || count>MAXIMUM_WAIT_OBJECTS) { SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED; }
 std::shared_ptr<ServerObject> objects[MAXIMUM_WAIT_OBJECTS];
 HANDLE native[MAXIMUM_WAIT_OBJECTS];
 // One snapshot prevents partial lookup across a concurrent close/duplicate.
 AcquireSRWLockShared(&server_lock);
 DWORD error=0;
 for(DWORD i=0;i<count;++i) {
  auto it=server_table.find(ids[i]);
  if(it==server_table.end()) { error=ERROR_INVALID_HANDLE; break; }
  objects[i]=it->second; native[i]=objects[i]->native;
  for(DWORD j=0;j<i;++j) if(native[j]==native[i]) error=ERROR_INVALID_PARAMETER;
  if(error) break;
 }
 ReleaseSRWLockShared(&server_lock);
 if(error) { SetLastError(error); return WAIT_FAILED; }
 return WaitForMultipleObjectsEx(count,native,all,timeout,FALSE);
}
