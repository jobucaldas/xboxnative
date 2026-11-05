#pragma once
// Included inside play_runtime. Keep mapped explicit loads and imported
// dependencies on the same guest application/SetDllDirectory search path.
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
    return path;
}
