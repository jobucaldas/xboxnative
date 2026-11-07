#pragma once
// Included inside play_runtime. Native loader notifications are serialized by
// its loader lock. Mapped images need the same serialization and must remain
// rooted while a callback recursively calls LoadLibrary or FreeLibrary.
struct GuestThreadNotifications {
    std::lock_guard<LoaderMutex> lock;
    std::vector<Module*> modules;
    GuestThreadNotifications():lock(state->loaderMutex) {
        for(auto module:state->order)if(module->initialized)modules.push_back(module);
        for(auto module:modules)++module->notifications;
    }
    ~GuestThreadNotifications() {
        for(auto module:modules)--module->notifications;
        collectGuestModules();
    }
};
static bool attachGuestThread() {
    GuestThreadNotifications batch;
    // Initialize every template before callbacks can read a sibling's TLS.
    for(auto module:batch.modules)if(!module->image.initThreadStorage())return false;
    // A DLL loaded by a callback receives PROCESS_ATTACH on this thread;
    // it must not also receive THREAD_ATTACH for the same thread creation.
    for(auto module:batch.modules)if(!module->image.notifyThread(DLL_THREAD_ATTACH))return false;
    return true;
}
static void notifyGuestThreadDetach() {
    GuestThreadNotifications batch;
    for(auto i=batch.modules.rbegin();i!=batch.modules.rend();++i)
        (*i)->image.detachThread(DLL_THREAD_DETACH);
}
