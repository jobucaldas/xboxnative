// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for the Vulkan driver registry's replace-not-append rule (shared/vulkan_driver_registry.h, #177).
// HKLM\SOFTWARE\Khronos\Vulkan\Drivers survives a launcher upgrade and the Khronos loader keeps the first entry it
// enumerates per manifest basename, so a vulkan-dzn.json left by an earlier install shadows the live one and the
// guest sees no driver (exit-12). Each "install" below runs the same prune-then-write sequence as
// play_runtime.h publishVulkanDriverFiles against the regb registry, then reads the key back the way the loader does.
#include <cstdio>
#include <map>
#include <set>
#include "../../uwph/host/reg_builtin.h"
#include "../../shared/vulkan_driver_registry.h"

static int bad;
#define EXPECT(c) do { if(!(c)) { printf("VULKAN-REGISTRY FAIL line %d: %s\n",__LINE__,#c); bad++; } } while(0)
static const wchar_t* const kKey=L"SOFTWARE\\Khronos\\Vulkan\\Drivers";
static const std::vector<std::wstring> kManifests={L"vulkan-dzn.json",L"vulkan-null-icd.json"};

static void touch(const std::wstring& path) {
    FILE* f=_wfopen(path.c_str(),L"wb"); if(f) fclose(f);
}
// An install directory that really holds the manifests (and so passes the loader's own existence check).
static std::wstring make_install(const std::wstring& root,const std::wstring& name,bool withDzn=true) {
    std::wstring dir=root+L"\\"+name;
    CreateDirectoryW(dir.c_str(),nullptr);
    if(withDzn) touch(dir+L"\\vulkan-dzn.json");
    touch(dir+L"\\vulkan-null-icd.json");
    return dir;
}
// Mirrors publishVulkanDriverFiles after the vulkan-1.dll gate: list what is packaged, prune, write. Returns the
// number pruned, or -1 when nothing is registered.
static int register_install(const std::wstring& runtime) {
    std::vector<std::wstring> listed;
    for(auto& name:kManifests) { std::wstring m=runtime+L"\\"+name; if(vulkan_driver_registry::exists(m)) listed.push_back(m); }
    if(listed.empty()) return -1;
    HKEY key=nullptr;
    if(regb::RegCreateKeyExW(HKEY_LOCAL_MACHINE,kKey,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr)!=ERROR_SUCCESS) return -1;
    const int pruned=(int)vulkan_driver_registry::prune_stale(key,runtime,kManifests);
    DWORD zero=0;
    for(auto& m:listed) regb::RegSetValueExW(key,m.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero);
    regb::RegCloseKey(key);
    return pruned;
}
// The key as the loader enumerates it: wide names in enumeration order.
static std::vector<std::wstring> entries() {
    std::vector<std::wstring> out; HKEY key=nullptr;
    if(regb::RegOpenKeyExW(HKEY_LOCAL_MACHINE,kKey,0,KEY_READ,&key)!=ERROR_SUCCESS) return out;
    for(DWORD i=0;;++i) {
        wchar_t name[2048]; DWORD chars=2048;
        if(regb::RegEnumValueW(key,i,name,&chars,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS) break;
        out.push_back(std::wstring(name,chars));
    }
    regb::RegCloseKey(key);
    return out;
}
static bool has(const std::wstring& value) {
    for(auto& e:entries()) if(!_wcsicmp(e.c_str(),value.c_str())) return true;
    return false;
}
// What decides exit-12: no basename may be listed twice, because the loader keeps the first one it meets.
static bool no_shadowing() {
    std::set<std::wstring> seen;
    for(auto& e:entries()) {
        std::wstring leaf=e.substr(e.find_last_of(L"\\/")+1);
        for(auto& c:leaf) c=(wchar_t)towlower(c);
        if(!seen.insert(leaf).second) return false;
    }
    return true;
}
static bool only(const std::vector<std::wstring>& want) {
    auto got=entries();
    if(got.size()!=want.size()) return false;
    for(auto& w:want) if(!has(w)) return false;
    return true;
}
static void put(const std::wstring& value) {
    HKEY key=nullptr; DWORD zero=0;
    regb::RegCreateKeyExW(HKEY_LOCAL_MACHINE,kKey,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr);
    regb::RegSetValueExW(key,value.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero);
    regb::RegCloseKey(key);
}

int main(int argc,char** argv) {
    if(argc<2) { puts("usage: vulkan-registry-stale <writable dir>"); return 2; }
    std::wstring dir(argv[1],argv[1]+strlen(argv[1]));
    for(const wchar_t* f:{L"\\registry.json",L"\\registry.json.imported",L"\\registry.sqlite",L"\\registry.sqlite-wal",L"\\registry.sqlite-shm"}) DeleteFileW((dir+f).c_str());
    regb::init(dir,[](const char*){});
    const std::wstring a=make_install(dir,L"install-A"), b=make_install(dir,L"install-B"), c=make_install(dir,L"install-C",false);

    // First install: the key does not exist yet. Nothing to prune, exactly the live pair registered.
    EXPECT(entries().empty());
    EXPECT(register_install(a)==0);
    EXPECT(only({a+L"\\vulkan-dzn.json",a+L"\\vulkan-null-icd.json"}));

    // Registering the same install again is idempotent: no growth and nothing removed.
    EXPECT(register_install(a)==0);
    EXPECT(only({a+L"\\vulkan-dzn.json",a+L"\\vulkan-null-icd.json"}));

    // Upgrade to install B while A's manifests are still on disk (an old package directory is not cleaned up
    // synchronously). Existence cannot save them: A's entries must go because they are Play's manifests elsewhere.
    EXPECT(vulkan_driver_registry::exists(a+L"\\vulkan-dzn.json"));
    EXPECT(register_install(b)==2);
    EXPECT(only({b+L"\\vulkan-dzn.json",b+L"\\vulkan-null-icd.json"}));
    EXPECT(!has(a+L"\\vulkan-dzn.json") && !has(a+L"\\vulkan-null-icd.json"));
    EXPECT(no_shadowing());

    // A reload is what the next launch sees; the replacement must have been persisted.
    regb::init(dir,[](const char*){});
    EXPECT(only({b+L"\\vulkan-dzn.json",b+L"\\vulkan-null-icd.json"}));

    // An install whose package carries no dzn (a launcher built without it) registers only the null ICD and
    // still removes B's dzn entry: B's dzn manifest is stale for that runtime directory.
    EXPECT(register_install(c)==2);
    EXPECT(only({c+L"\\vulkan-null-icd.json"}));
    EXPECT(no_shadowing());

    // Several accumulated installs, spelled the way a hand-edited or older registry might: another case, forward
    // slashes, a directory that no longer exists. Whatever the spelling, only the live pair survives.
    put(a+L"\\VULKAN-DZN.JSON");
    put(L"C:/gone/old-package/vulkan-dzn.json");
    put(L"D:\\gone\\Vulkan-Null-Icd.json");
    EXPECT(entries().size()==4);
    EXPECT(register_install(b)==4);
    EXPECT(only({b+L"\\vulkan-dzn.json",b+L"\\vulkan-null-icd.json"}));
    EXPECT(no_shadowing());

    // The live entry is matched case-insensitively: a Windows path is the same path in any case, so it is kept
    // (not removed and re-added) and never duplicated.
    { std::wstring upper=b; for(auto& ch:upper) ch=(wchar_t)towupper(ch);
      EXPECT(register_install(upper)==0);          // same directory, other spelling: nothing is stale
      EXPECT(entries().size()==2); EXPECT(no_shadowing()); }

    // Unrelated entries: a third-party driver whose manifest exists is left alone across installs, one whose
    // manifest is gone is removed, and a name that merely contains ours (vulkan-dzn.json.bak) is not ours.
    const std::wstring third=dir+L"\\third-party";
    CreateDirectoryW(third.c_str(),nullptr);
    touch(third+L"\\vulkan_radeon.json"); touch(third+L"\\vulkan-dzn.json.bak");
    put(third+L"\\vulkan_radeon.json"); put(third+L"\\vulkan-dzn.json.bak"); put(dir+L"\\missing\\vulkan_intel.json");
    EXPECT(register_install(a)==3);   // B's pair plus the missing third-party manifest
    EXPECT(only({a+L"\\vulkan-dzn.json",a+L"\\vulkan-null-icd.json",third+L"\\vulkan_radeon.json",third+L"\\vulkan-dzn.json.bak"}));
    EXPECT(no_shadowing());

    // A third-party "vulkan-dzn.json" is the same basename the loader de-duplicates on, so it is stale too.
    touch(third+L"\\vulkan-dzn.json"); put(third+L"\\vulkan-dzn.json");
    EXPECT(register_install(a)==1);
    EXPECT(!has(third+L"\\vulkan-dzn.json") && has(third+L"\\vulkan_radeon.json"));

    // A value name longer than the enumeration buffer is skipped without ending the scan, so stale entries that
    // sort after it are still removed.
    put(dir+L"\\"+std::wstring(1500,L'x')+L"\\vulkan-dzn.json");
    put(L"Z:\\late-stale\\vulkan-dzn.json");
    EXPECT(register_install(a)>=1);
    EXPECT(!has(L"Z:\\late-stale\\vulkan-dzn.json"));

    // Registering nothing (runtime without manifests) must leave the key untouched.
    const std::wstring empty=dir+L"\\install-empty"; CreateDirectoryW(empty.c_str(),nullptr);
    const size_t before=entries().size();
    EXPECT(register_install(empty)==-1);
    EXPECT(entries().size()==before);

    printf(bad?"VULKAN-REGISTRY FAILED %d\n":"VULKAN-REGISTRY OK\n",bad);
    return bad!=0;
}
