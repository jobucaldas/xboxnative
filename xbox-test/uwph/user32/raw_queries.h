#pragma once
// Wine 10 win32u/rawinput.c query validation order. This record backend has
// no HID devices or WM_INPUT producer, so it cannot issue a valid raw handle.
extern "C" UINT WINAPI c_GetRawInputDeviceInfoW(HANDLE,UINT command,LPVOID,PUINT size){
 if(!size){SetLastError(ERROR_NOACCESS);return UINT(-1);}
 if(command!=RIDI_DEVICENAME&&command!=RIDI_DEVICEINFO&&command!=RIDI_PREPARSEDDATA){
  SetLastError(ERROR_INVALID_PARAMETER);return UINT(-1);
 }
 SetLastError(ERROR_INVALID_HANDLE);return UINT(-1);
}
extern "C" UINT WINAPI c_GetRawInputData(HANDLE,UINT,LPVOID,PUINT,UINT){
 // Wine checks the current raw message handle before all other arguments.
 // A NULL data pointer queries an existing record; it is not empty success.
 SetLastError(ERROR_INVALID_HANDLE);return UINT(-1);
}
extern "C" INT WINAPI c_GetRawInputBuffer(LPVOID,PUINT size,UINT headerSize){
 if(!size||headerSize!=sizeof(RawInputHeader2)){SetLastError(ERROR_INVALID_PARAMETER);return -1;}
 // Wine server/queue.c get_rawinput_buffer reports no next record as zero,
 // both for size queries and reads. Preserve last error on empty success.
 *size=0;return 0;
}
