#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Play's ICD registration in HKLM\SOFTWARE\Khronos\Vulkan\Drivers.
//
// The Khronos loader de-duplicates that key by manifest basename and keeps the first entry it enumerates
// (loader_windows.c: a later "vulkan-dzn.json" is logged as "Skipping adding of json file ... due to duplication"),
// so a value left by an earlier install shadows the live manifest and the loader reports no driver at all. The key is
// machine-wide and survives an upgrade, and Play only ever deleted the manifests of the directory it was given, so
// every install accumulated its own pair. Registration therefore replaces rather than appends: before the current
// pair is written, every entry that cannot be the live one is removed.
//
// Only two kinds of entry are removed: Play's own manifests (vulkan-dzn.json, vulkan-null-icd.json) that point
// anywhere but the runtime directory being registered, and any entry whose manifest file no longer exists. A
// third-party driver whose manifest is still on disk is left alone.
#include <windows.h>
#include <string>
#include <vector>
#include "../uwph/host/reg_builtin.h"

namespace vulkan_driver_registry {
static bool exists(const std::wstring& path) {
    DWORD a=GetFileAttributesW(path.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring leaf(const std::wstring& value) {
    auto slash=value.find_last_of(L"\\/");
    return slash==std::wstring::npos?value:value.substr(slash+1);
}
// Deletes the values of `key` that the registration for `runtime` supersedes. Returns how many were removed.
static unsigned prune_stale(HKEY key,const std::wstring& runtime,const std::vector<std::wstring>& manifests) {
    std::vector<std::wstring> stale;
    for(DWORD index=0;;++index) {
        wchar_t name[1024]; DWORD chars=DWORD(sizeof name/sizeof name[0]);
        LONG e=regb::RegEnumValueW(key,index,name,&chars,nullptr,nullptr,nullptr,nullptr);
        if(e==ERROR_NO_MORE_ITEMS) break;
        if(e==ERROR_MORE_DATA) continue;   // a name longer than any manifest path this function owns
        if(e!=ERROR_SUCCESS) break;        // an unreadable value means the key is not ours to prune
        std::wstring value(name,chars), name_leaf=leaf(value);
        bool ours=false;
        for(auto& manifest:manifests) {
            if(_wcsicmp(name_leaf.c_str(),manifest.c_str())) continue;
            ours=true;
            // The live entry is the one this registration is about to write; anything else with our name is stale.
            if(!_wcsicmp(value.c_str(),(runtime+L"\\"+manifest).c_str())) break;
            stale.push_back(value);
            break;
        }
        // An entry whose manifest is gone is dead weight: the loader would read a path that is not there.
        if(!ours && !exists(value)) stale.push_back(value);
    }
    unsigned removed=0;
    for(auto& value:stale) if(regb::RegDeleteValueW(key,value.c_str())==ERROR_SUCCESS) ++removed;
    return removed;
}
}  // namespace vulkan_driver_registry
