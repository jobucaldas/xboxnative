// Off-device regression against the packaged record DLL, not host user32.
// Resolves by name so a missing export fails the check instead of the link.
#include <windows.h>
#include <cstdio>

static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)

typedef BOOL (WINAPI *EnableFn)(BOOL);
typedef BOOL (WINAPI *IsEnabledFn)(void);

int main(int argc,char** argv){
 if(argc<2){printf("usage: test.exe <uwph-u32c.dll>\n");return 2;}
 HMODULE dll=LoadLibraryA(argv[1]);
 CHECK(dll!=nullptr);
 if(!dll) return 1;
 auto enable=(EnableFn)GetProcAddress(dll,"EnableMouseInPointer");
 auto isEnabled=(IsEnabledFn)GetProcAddress(dll,"IsMouseInPointerEnabled");
 CHECK(enable!=nullptr);
 CHECK(isEnabled!=nullptr);
 if(enable&&isEnabled){
  CHECK(isEnabled()==FALSE);
  CHECK(enable(FALSE)==TRUE);
  CHECK(isEnabled()==FALSE);
  SetLastError(0);
  CHECK(enable(TRUE)==FALSE);
  CHECK(GetLastError()==ERROR_NOT_SUPPORTED);
  CHECK(isEnabled()==FALSE);
 }
 printf("mouse-pointer checks=%u failures=%u\n",checks,failures);
 FreeLibrary(dll);
 return failures?1:0;
}
