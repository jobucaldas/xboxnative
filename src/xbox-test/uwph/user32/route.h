#pragma once
// Shared by launcher and host MLP probes. Never changes the CoreWindow adapter.
namespace user32_core {
inline uintptr_t resolve(const char* dll,const char* name) {
 if(!name||_stricmp(dll,"user32.dll"))return 0;
 static HMODULE module=LoadPackagedLibrary(L"uwph-u32c.dll",0);
 return module?(uintptr_t)GetProcAddress(module,name):0;
}
}
