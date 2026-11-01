#pragma once
// Included inside play_runtime after its module registry helpers. These
// queries must see mapped guest images, which are not OS loader entries.
static void* WINAPI kernelPcToFileHeader(void* pc, void** base) {
    for (auto& module : state->modules) {
        uintptr_t start = reinterpret_cast<uintptr_t>(module->image.base);
        uintptr_t address = reinterpret_cast<uintptr_t>(pc);
        if (address >= start && address - start < module->image.size) {
            *base = module->image.base;
            return *base;
        }
    }
    return RtlPcToFileHeader(pc, base);
}
static DWORD WINAPI kernelModuleBaseName(HANDLE process, HMODULE handle, LPWSTR output, DWORD capacity) {
    Module* module = handle ? find(handle) : state->exe;
    // Native modules and remote processes still use the documented OS query.
    if (module && (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())) {
        if (!capacity) return 0;
        if (!output) { SetLastError(ERROR_NOACCESS); return 0; }
        auto separator = module->path.find_last_of(L"\\/");
        auto name = module->path.substr(separator == std::wstring::npos ? 0 : separator + 1);
        DWORD length = static_cast<DWORD>(std::min<size_t>(capacity, name.size()));
        memcpy(output, name.data(), length * sizeof(wchar_t));
        if (length < capacity) output[length] = 0;
        return length;
    }
    using Query = DWORD (WINAPI*)(HANDLE, HMODULE, LPWSTR, DWORD);
    static Query query = []() -> Query {
        HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto p = module ? GetProcAddress(module, "K32GetModuleBaseNameW") : nullptr;
        return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
    }();
    if (!query) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
    return query(process, handle, output, capacity);
}
