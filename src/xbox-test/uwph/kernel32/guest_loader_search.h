#pragma once
// Included inside play_runtime. Keep mapped explicit loads and imported
// dependencies on the same guest application/SetDllDirectory search path.
// VC++ redistributable DLLs the console lacks; Play ships Wine's builds in its
// runtime directory and maps them, after the game's own copies, the way an
// installed redistributable serves them from System32 on Windows.
static std::wstring runtimeDirectory;
static void set_runtime_directory(const std::wstring& path) { runtimeDirectory=path; }
// The OpenGL stack (Mesa's d3d12 driver, research/OPENGL-PLAN.md) is served from the same directory: opengl32.dll
// and its driver library libgallium_wgl.dll, as the OS opengl32.dll serves a game on Windows. The console has none,
// so a game that ships no GL DLL of its own gets these (when the package carries them; otherwise Play's WGL stubs
// answer). dxil.dll is the optional DXIL signer Mesa loads by name (DXIL.dll) for shader validation.
// Vulkan (research/VULKAN-PLAN.md) is the same contract: vulkan-1.dll is the Khronos loader built from pinned source,
// vulkan_dzn.dll is Mesa's dzn driver (Vulkan on D3D12) and vulkan-null-icd.dll is the fallback driver that reports
// zero physical devices. Both manifests name a bare DLL, so a Vulkan-optional engine's LoadLibrary succeeds and the
// loader's own LoadLibraryW of the ICD resolves here.
static bool redistributable(const std::wstring& name) {
    std::wstring key=name;for(auto& c:key)c=towlower(c);
    for(auto dll:{L"opengl32.dll",L"libgallium_wgl.dll",L"dxil.dll",L"vulkan-1.dll",L"vulkan_dzn.dll",L"vulkan-null-icd.dll",L"msvcp140.dll",L"msvcp140_1.dll",L"msvcp140_2.dll",L"msvcp140_atomic_wait.dll",
                  L"msvcp140_codecvt_ids.dll",L"concrt140.dll"}) if(key==dll) return true;
    return false;
}
// The console ships d3d11.dll and dxgi.dll but no d3d9.dll, d3d10.dll,
// d3d10_1.dll or d3d10core.dll, so a guest importing one of those fails to
// load. Play ships its own D3D9 and D3D10 shims beside the launcher and maps
// them like the Wine donors: a guest copy in the game folder still wins, and
// nothing is loaded from the host's System32.
static bool graphics_shim(const std::wstring& name) {
    std::wstring key=name;for(auto& c:key)c=towlower(c);
    for(auto dll:{L"d3d9.dll",L"d3d10.dll",L"d3d10_1.dll",L"d3d10core.dll"}) if(key==dll) return true;
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
// An absolute path into the OS directory names the OS module, never a guest
// image: Windows resolves LoadLibraryW("C:\Windows\System32\dxgi.dll") to the
// system loader's own dxgi. The Vulkan loader does exactly that in
// windows_initialization (its ICD discovery calls CreateDXGIFactory1 through
// it), so mapping the file as a guest image would hand it a module whose
// user32 imports Play refuses and leave its factory pointer NULL.
static bool systemDirectoryPath(const std::wstring& path) {
    using GetDir=DWORD(WINAPI*)(LPWSTR,DWORD);
    auto get=(GetDir)kernel_tail::native("GetSystemDirectoryW");
    wchar_t directory[MAX_PATH]{};DWORD n=get?get(directory,MAX_PATH):0;
    if(!get||!n||n>=MAX_PATH)return false;
    std::wstring prefix(directory,n);
    if(path.size()<=prefix.size()||_wcsnicmp(path.c_str(),prefix.c_str(),prefix.size()))return false;
    return path[prefix.size()]==L'\\' && path.find(L'\\',prefix.size()+1)==std::wstring::npos;
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
    if(!runtimeDirectory.empty() && (redistributable(requested) || graphics_shim(requested))) {
        auto candidate=runtimeDirectory+L"\\"+requested;
        if(exists(candidate))return candidate;
    }
    return path;
}
