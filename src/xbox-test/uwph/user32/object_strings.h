#pragma once
// Wine 10 user32/winstation.c GetUserObjectInformationA: insufficient
// ANSI buffers report the Unicode byte size, but success reports ANSI bytes.
// Desktop names in this backend are fixed ASCII, so both sizes are exact.
inline BOOL object_string(const char* text,void* info,DWORD len,DWORD* needed) {
 const DWORD bytes=static_cast<DWORD>(strlen(text))+1;
 if(needed)*needed=bytes;
 if(len<bytes){
  if(needed)*needed=bytes*sizeof(WCHAR);
  SetLastError(ERROR_INSUFFICIENT_BUFFER);
  return FALSE;
 }
 if(info)memcpy(info,text,bytes);
 return TRUE;
}
