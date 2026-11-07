#pragma once
// Wine 10 win32u/winstation.c NtUserGetObjectInformation(UOI_FLAGS).
// USEROBJECTFLAGS is two BOOLs followed by DWORD, including for desktops.
// Wine leaves the inheritance/reserved fields untouched. Desktop flags are
// zero here: WSF_VISIBLE belongs to window stations, not desktop objects.
struct ObjectFlags { BOOL inherit, reserved; DWORD flags; };
static_assert(sizeof(ObjectFlags)==12,"USEROBJECTFLAGS Win64 ABI");
// The interactive window station is visible (WSF_VISIBLE): Unity checks it to
// tell an interactive session from a service.
inline BOOL object_flags(void* info,DWORD len,DWORD* needed,DWORD flags=0) {
 if(needed)*needed=sizeof(ObjectFlags);
 if(len<sizeof(ObjectFlags)){SetLastError(ERROR_BUFFER_OVERFLOW);return FALSE;}
 if(!info){SetLastError(ERROR_NOACCESS);return FALSE;}
 static_cast<ObjectFlags*>(info)->flags=flags;
 return TRUE;
}
