#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 HMODULE m=LoadLibraryW(L"uwph-u32c.dll");CHECK(m);
 auto query=reinterpret_cast<decltype(&GetUserObjectInformationA)>(GetProcAddress(m,"GetUserObjectInformationA"));
 auto desktop=reinterpret_cast<decltype(&GetThreadDesktop)>(GetProcAddress(m,"GetThreadDesktop"));CHECK(query&&desktop);
 HANDLE guest=desktop(GetCurrentThreadId());
 for(DWORD len=0;len<=32;++len){
  unsigned char actual[40],expected[40];memset(actual,0xa5,40);memset(expected,0xa5,40);
  DWORD need=0,reference=0;SetLastError(0xdead);
  BOOL want=GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()),UOI_FLAGS,expected,len,&reference);
  DWORD error=GetLastError();SetLastError(0xdead);
  CHECK(query(guest,UOI_FLAGS,actual,len,&need)==want);
  CHECK(need==reference&&need==sizeof(USEROBJECTFLAGS));
  CHECK(GetLastError()==error);CHECK(!memcmp(actual,expected,40));
 }
 // ANSI string queries report Unicode bytes on insufficient buffers,
 // ANSI bytes on success, and permit NULL output when length suffices.
 for(int index : {UOI_NAME,UOI_TYPE}) for(DWORD len=0;len<=24;++len)
 for(bool null_output : {false,true}) {
  unsigned char actual[40],expected[40];memset(actual,0xa5,40);memset(expected,0xa5,40);
  DWORD need=0,reference=0;SetLastError(0xdead);
  BOOL want=GetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()),index,null_output?nullptr:expected,len,&reference);
  DWORD error=GetLastError();SetLastError(0xdead);
  BOOL got=query(guest,index,null_output?nullptr:actual,len,&need);
  if(got!=want||need!=reference||GetLastError()!=error||memcmp(actual,expected,40))
   printf("string index=%d len=%lu null=%d candidate=%d/%lu/%lu Wine=%d/%lu/%lu\n",index,len,null_output,got,need,GetLastError(),want,reference,error);
  CHECK(got==want&&need==reference&&GetLastError()==error);
  CHECK(!memcmp(actual,expected,40));
 }
 DWORD needed=0;SetLastError(0xdead);
 CHECK(!query(guest,UOI_FLAGS,nullptr,12,&needed)&&GetLastError()==ERROR_NOACCESS&&needed==12);
 SetLastError(0xdead);CHECK(!query(reinterpret_cast<HANDLE>(1),UOI_FLAGS,nullptr,0,&needed));
 CHECK(GetLastError()==ERROR_INVALID_HANDLE);
 puts("PASS: 33 UOI_FLAGS and 100 ANSI string Wine comparisons; NULL and foreign-handle isolation");
}
