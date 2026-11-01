#pragma once
// Included inside play_runtime. Explicit references and pins root the mapped
// dependency graph. The executable and failed diagnostic images stay resident.
// Reachability, rather than per-edge reference counts, handles import cycles.
static BOOL WINAPI moduleExW(DWORD flags,LPCWSTR name,HMODULE* out) {
    constexpr DWORD supported=GET_MODULE_HANDLE_EX_FLAG_PIN|
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;
    if(!out) {SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    *out=nullptr;
    if((flags&~supported)||((flags&GET_MODULE_HANDLE_EX_FLAG_PIN)&&
                           (flags&GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {
        SetLastError(ERROR_INVALID_PARAMETER);return FALSE;
    }
    std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
    Module* module=nullptr;
    if((flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)&&name) {
        for(auto& candidate:state->modules) {
            auto address=reinterpret_cast<uintptr_t>(name);
            auto base=reinterpret_cast<uintptr_t>(candidate->image.base);
            if(address>=base && address-base<candidate->image.size) {module=candidate.get();break;}
        }
    } else module=name?named(name):state->exe;
    if(module) {
        if(flags&GET_MODULE_HANDLE_EX_FLAG_PIN)module->pinned=true;
        else if(!(flags&GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))++module->references;
        *out=reinterpret_cast<HMODULE>(module->image.base);return TRUE;
    }
    using Function=BOOL(WINAPI*)(DWORD,LPCWSTR,HMODULE*);
    auto function=reinterpret_cast<Function>(kernel_tail::native("GetModuleHandleExW"));
    if(!function) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
    return function(flags,name,out);
}
static std::vector<Module*> liveGuestModules() {
    std::vector<Module*> live;
    for(auto& module:state->modules)
        if(module.get()==state->exe || module->references || module->notifications || module->pinned ||
           module->initializing || module->initializationFailed)live.push_back(module.get());
    for(size_t i=0;i<live.size();++i)
        for(auto dependency:live[i]->dependencies)
            if(std::find(live.begin(),live.end(),dependency)==live.end())live.push_back(dependency);
    return live;
}
static void collectGuestModules() {
    // Like Wine's process_detach, keep loader identity and every image's pages
    // available during callbacks. Recompute roots after each callback sequence:
    // DllMain may load/release another DLL under the recursive loader lock.
    static thread_local bool collecting=false;
    if(collecting)return;
    collecting=true;
    struct Reset {bool& value;~Reset(){value=false;}} reset{collecting};
    std::vector<Module*> live;
    for(;;) {
        live=liveGuestModules();
        Module* pending=nullptr;
        for(auto i=state->order.rbegin();i!=state->order.rend();++i)
            if(std::find(live.begin(),live.end(),*i)==live.end()&&
               ((*i)->image.attached||(*i)->image.tlsAttached)) {pending=*i;break;}
        if(!pending)break;
        auto& image=pending->image;
        if(image.attached) {
            image.attached=false;
            image.dllEntry((HINSTANCE)image.base,DLL_PROCESS_DETACH,nullptr);
            image.log("WXL DllMain DLL_PROCESS_DETACH");
        }
        if(image.tlsAttached) {
            image.tlsAttached=false;
            image.detachThread(DLL_PROCESS_DETACH);
        }
    }
    // No callbacks remain. Remove registry entries before freeing any pages.
    std::vector<std::unique_ptr<Module>> retired;
    for(auto i=state->order.rbegin();i!=state->order.rend();++i) {
        auto module=*i;
        if(std::find(live.begin(),live.end(),module)!=live.end())continue;
        for(auto& owned:state->modules)if(owned.get()==module) {
            mlp::assets.erase(module->key);
            retired.push_back(std::move(owned));break;
        }
    }
    state->order.erase(std::remove_if(state->order.begin(),state->order.end(),
        [&](Module* module){return std::find(live.begin(),live.end(),module)==live.end();}),state->order.end());
    state->modules.erase(std::remove_if(state->modules.begin(),state->modules.end(),
        [](const std::unique_ptr<Module>& module){return !module;}),state->modules.end());
    state->moduleCount=int(state->modules.size());
    state->mappedCount=int(state->order.size());
    // Destructors now only deregister unwind metadata and release TLS/pages.
    for(auto& module:retired)module.reset();
}
static BOOL WINAPI freeLibrary(HMODULE handle) {
    std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
    if(auto module=find(handle)) {
        if(module->references)--module->references;
        collectGuestModules();return TRUE;
    }
    return FreeLibrary(handle);
}
