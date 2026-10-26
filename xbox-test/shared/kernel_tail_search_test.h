#pragma once
namespace kernel_tail {
inline unsigned test_search(void (*log)(const char*)) {
    unsigned checks=0,failures=0;
    auto check=[&](bool pass,const char* label){++checks;if(!pass)++failures;char line[192];snprintf(line,sizeof line,"K32T SEARCH-CHECK %s pass=%u error=%lu",label,unsigned(pass),GetLastError());log(line);};
    wchar_t executable[32768]{};
    auto module_path=(DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD))native("GetModuleFileNameW");
    DWORD n=module_path?module_path(nullptr,executable,32768):0;check(n&&n<32768,"package-path");
    std::wstring directory=executable;auto slash=directory.find_last_of(L"\\/");
    if(n&&slash!=std::wstring::npos) {
        directory.resize(slash);directory+=L"\\k32t";
        auto& state=search();std::wstring old;bool configured;
        {std::lock_guard<std::mutex> lock(state.mutex);old=state.directory;configured=state.configured;}
        check(set_directory(directory.c_str()),"set-packaged-subdirectory");
        HMODULE module=load_w(L"directory-test.dll");check(!!module,"load-from-directory");
        if(module){auto value=(int(*)())GetProcAddress(module,"k32t_search_value");check(value&&value()==42,"loaded-export-executes");check(FreeLibrary(module),"unload-fixture");}
        check(set_directory(configured?old.c_str():nullptr),"restore-search-state");
    }
    char line[144];snprintf(line,sizeof line,"K32T SEARCH-SELFTEST checks=%u failures=%u packaged-fixture no-game-code",checks,failures);log(line);return failures;
}
}
