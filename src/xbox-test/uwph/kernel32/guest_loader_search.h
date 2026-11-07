#pragma once
// Included inside play_runtime. Keep mapped explicit loads and imported
// dependencies on the same guest application/SetDllDirectory search path.
// VC++ redistributable DLLs the console lacks; Play ships Wine's builds in its
// runtime directory and maps them, after the game's own copies, the way an
// installed redistributable serves them from System32 on Windows.
static std::wstring runtimeDirectory;
static void set_runtime_directory(const std::wstring& path) { runtimeDirectory=path; }
static bool redistributable(const std::wstring& name) {
    std::wstring key=name;for(auto& c:key)c=towlower(c);
    for(auto dll:{L"msvcp140.dll",L"msvcp140_1.dll",L"msvcp140_2.dll",L"msvcp140_atomic_wait.dll",
                  L"msvcp140_codecvt_ids.dll",L"concrt140.dll"}) if(key==dll) return true;
    return false;
}
// API-set names (api-ms-win-*, ext-ms-win-*) always resolve through the OS
// schema on Windows 10; app-local copies (old VC++ app-local UCRT deployments
// ship api-ms-win-crt-*.dll beside the game) are never loaded. Play does the
// same: such a name is never mapped from the game's folder.
static bool apiSetName(const std::wstring& name) {
    std::wstring key=name;for(auto& c:key)c=towlower(c);
    auto slash=key.find_last_of(L"\\/");if(slash!=std::wstring::npos)key=key.substr(slash+1);
    return key.rfind(L"api-ms-win-",0)==0||key.rfind(L"ext-ms-win-",0)==0;
}
static std::wstring guestLibraryPath(const std::wstring& requested,bool useDirectory=true) {
    std::wstring path=requested;
    if(path.find(L':')==std::wstring::npos && (path.empty()||path[0]!=L'\\'))
        path=state->root+L"\\"+path;
    if(!useDirectory || exists(path) || requested.find_first_of(L"\\/:")!=std::wstring::npos)
        return path;
    auto& search=kernel_tail::search();
    std::lock_guard<std::mutex> lock(search.mutex);
    if(search.configured && !search.directory.empty()) {
        auto candidate=search.directory+L"\\"+requested;
        if(exists(candidate))return candidate;
    }
    if(!runtimeDirectory.empty() && redistributable(requested)) {
        auto candidate=runtimeDirectory+L"\\"+requested;
        if(exists(candidate))return candidate;
    }
    return path;
}
