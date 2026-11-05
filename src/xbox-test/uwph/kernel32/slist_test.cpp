#include "route.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name, F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name));
    CHECK(p==kernel32_core::resolve("api-ms-win-core-interlocked-l1-1-0.dll",name));
    return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
struct alignas(16) Node { SLIST_ENTRY entry; unsigned index; };
alignas(16) static SLIST_HEADER head;
static constexpr unsigned threads=4, count=512;
static Node nodes[threads*count];
static LONG seen[threads*count];
static LONG invalid;
static auto push=&::InterlockedPushEntrySList;
static auto pop=&::InterlockedPopEntrySList;
static DWORD WINAPI producer(void* opaque) {
    auto id=static_cast<unsigned>(reinterpret_cast<uintptr_t>(opaque));
    for(unsigned i=id*count;i<(id+1)*count;++i) {
        nodes[i].index=i;
        push(&head,&nodes[i].entry);
    }
    return 0;
}
static DWORD WINAPI consumer(void*) {
    while(auto entry=pop(&head)) {
        auto node=reinterpret_cast<Node*>(entry);
        if(node<nodes || node>=nodes+threads*count || node->index>=threads*count)
            InterlockedIncrement(&invalid);
        else InterlockedIncrement(&seen[node->index]);
    }
    return 0;
}
int main() {
    auto init=API(InitializeSListHead);
    push=API(InterlockedPushEntrySList); pop=API(InterlockedPopEntrySList);
    auto flush=API(InterlockedFlushSList);
    SetLastError(1234); init(&head); CHECK(GetLastError()==1234);
    CHECK(QueryDepthSList(&head)==0); CHECK(!pop(&head)); CHECK(!flush(&head));
    Node a{},b{},c{};
    CHECK(!push(&head,&a.entry)); CHECK(push(&head,&b.entry)==&a.entry);
    CHECK(push(&head,&c.entry)==&b.entry); CHECK(QueryDepthSList(&head)==3);
    CHECK(pop(&head)==&c.entry); CHECK(QueryDepthSList(&head)==2);
    auto chain=flush(&head); CHECK(chain==&b.entry);
    CHECK(chain->Next==&a.entry); CHECK(!chain->Next->Next);
    CHECK(!pop(&head)); CHECK(QueryDepthSList(&head)==0);
    HANDLE workers[threads];
    for(unsigned i=0;i<threads;++i) { workers[i]=CreateThread(nullptr,0,producer,reinterpret_cast<void*>(uintptr_t(i)),0,nullptr); CHECK(workers[i]); }
    CHECK(WaitForMultipleObjects(threads,workers,TRUE,10000)==WAIT_OBJECT_0);
    for(auto worker:workers) CHECK(CloseHandle(worker));
    CHECK(QueryDepthSList(&head)==threads*count);
    for(unsigned i=0;i<threads;++i) { workers[i]=CreateThread(nullptr,0,consumer,nullptr,0,nullptr); CHECK(workers[i]); }
    CHECK(WaitForMultipleObjects(threads,workers,TRUE,10000)==WAIT_OBJECT_0);
    for(auto worker:workers) CHECK(CloseHandle(worker));
    CHECK(!invalid); for(auto value:seen) CHECK(value==1);
    CHECK(!flush(&head)); CHECK(QueryDepthSList(&head)==0);
    CHECK(!kernel32_core::resolve("api-ms-win-core-interlocked-l1-1-0.dll","LocalAlloc"));
    CHECK(!kernel32_core::resolve("unrelated.dll","InterlockedPopEntrySList"));
    std::printf("kernel32 slist: PASS (%u checks; off-device native atomic ABI)\n",checks);
}
