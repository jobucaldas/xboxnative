#pragma once
// Wine 10 win32u/rawinput.c: validate the whole batch before registration;
// keep one record per (usage page, usage), sorted by that pair. This is
// guest registration bookkeeping only; no WM_INPUT/device delivery exists.
extern "C" BOOL WINAPI c_RegisterRawInputDevices(const RawInputDevice2* devices,UINT count,UINT size){
 if(size!=sizeof(RawInputDevice2)||(!devices&&count)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
 std::lock_guard<std::recursive_mutex> l(mutex);
 for(UINT i=0;i<count;++i){const auto& d=devices[i];
  if(((d.dwFlags&RIDEV_INPUTSINK)&&!d.hwndTarget)||((d.dwFlags&RIDEV_REMOVE)&&d.hwndTarget)){
   SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
  }
  // Never pass guest HWNDs to native USER32. Preserve the existing guest
  // registry validation, but perform it before mutating any records.
  if(d.hwndTarget&&!window(d.hwndTarget))return FALSE;
 }
 for(UINT i=0;i<count;++i){const auto& d=devices[i];
  auto pos=std::lower_bound(rawDevices.begin(),rawDevices.end(),d,[](const auto& a,const auto& b){
   return a.usUsagePage<b.usUsagePage||(a.usUsagePage==b.usUsagePage&&a.usUsage<b.usUsage);
  });
  bool found=pos!=rawDevices.end()&&pos->usUsagePage==d.usUsagePage&&pos->usUsage==d.usUsage;
  if(d.dwFlags&RIDEV_REMOVE){if(found)rawDevices.erase(pos);}
  else if(found)*pos=d;
  else rawDevices.insert(pos,d);
 }
 return TRUE;
}
extern "C" UINT WINAPI c_GetRegisteredRawInputDevices(RawInputDevice2* devices,UINT* count,UINT size){
 if(size!=sizeof(RawInputDevice2)||!count||(devices&&!*count)){
  SetLastError(ERROR_INVALID_PARAMETER);return UINT(-1);
 }
 std::lock_guard<std::recursive_mutex> l(mutex);
 UINT capacity=*count;*count=static_cast<UINT>(rawDevices.size());
 if(!devices)return 0;
 if(capacity<*count){SetLastError(ERROR_INSUFFICIENT_BUFFER);return UINT(-1);}
 if(*count)memcpy(devices,rawDevices.data(),size_t(*count)*sizeof(*devices));
 return *count;
}
