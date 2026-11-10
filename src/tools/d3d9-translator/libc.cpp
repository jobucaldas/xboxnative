// SPDX-License-Identifier: GPL-3.0-or-later
// The libc surface the donor's objects and libstdc++ need, over kernel32 only.
//
// Play maps this DLL inside the launcher's process, so there is no CRT to link
// against (see src/xbox-test/uwph/d3dshim/base.h): every allocation goes to the
// process heap, and nothing here opens a file, formats through stdio or starts
// a thread. The compiler still lowers struct copies, comparisons and container
// growth to these names, and libstdc++'s headers call the guard and throw
// helpers, so they are supplied here rather than pulled in from a library the
// console does not have.
//
// Build with -fno-builtin -fno-tree-loop-distribute-patterns: without them the
// compiler turns these loops back into calls to themselves.
#include <windows.h>
extern "C" {
void* malloc(size_t n){return HeapAlloc(GetProcessHeap(),0,n?n:1);}
void free(void* p){if(p)HeapFree(GetProcessHeap(),0,p);}
void* calloc(size_t n,size_t s){const size_t bytes=n*s;return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes?bytes:1);}
void* realloc(void* p,size_t n){return p?HeapReAlloc(GetProcessHeap(),0,p,n?n:1):HeapAlloc(GetProcessHeap(),0,n?n:1);}
void abort(void){ExitProcess(3);}
void* memmove(void* d,const void* s,size_t n){auto a=(unsigned char*)d;auto b=(const unsigned char*)s;
  if(a<b){while(n--)*a++=*b++;}else{a+=n;b+=n;while(n--)*--a=*--b;}return d;}
void* memchr(const void* s,int c,size_t n){auto p=(const unsigned char*)s;while(n--){if(*p==(unsigned char)c)return (void*)p;++p;}return nullptr;}
size_t strlen(const char* s){size_t n=0;while(s[n])++n;return n;}
int strcmp(const char* a,const char* b){while(*a&&*a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
void* memset(void* d,int c,size_t n){auto p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;}
void* memcpy(void* d,const void* s,size_t n){auto a=(unsigned char*)d;auto b=(const unsigned char*)s;while(n--)*a++=*b++;return d;}
int memcmp(const void* a,const void* b,size_t n){auto x=(const unsigned char*)a;auto y=(const unsigned char*)b;while(n--){if(*x!=*y)return *x<*y?-1:1;++x;++y;}return 0;}
void* operator new(size_t n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,size_t) noexcept {free(p);}
void* operator new[](size_t n){return malloc(n);}
void operator delete[](void* p) noexcept {free(p);}
void operator delete[](void* p,size_t) noexcept {free(p);}
void ___chkstk_ms(void) {}
int atexit(void(*)(void)){return 0;}
void __cxa_pure_virtual(){}
// Function-local statics in the donor's tables. The translator runs on the
// thread that created the device, and no two threads translate at once, so a
// plain flag is enough; a racing first call would still only repeat an
// idempotent table build.
int __cxa_guard_acquire(long long* guard){return *(volatile char*)guard==0;}
void __cxa_guard_release(long long* guard){*(volatile char*)guard=1;}
void __cxa_guard_abort(long long*) {}
// The donor formats constant-buffer, sampler and texture names with snprintf.
// Only %d and %s ever appear; anything else is copied through rather than
// guessed at.
int __mingw_snprintf(char* out,size_t size,const char* fmt,...) {
    if(!out||!size)return 0;
    __builtin_va_list args;__builtin_va_start(args,fmt);
    size_t used=0;
    for(const char* p=fmt;*p;++p) {
        if(*p!='%'){if(used+1<size)out[used]=*p;++used;continue;}
        ++p;
        if(*p=='d') {
            int value=__builtin_va_arg(args,int);
            char digits[12];unsigned n=0;bool negative=value<0;
            unsigned long long magnitude=negative?(unsigned long long)(-(long long)value):(unsigned long long)value;
            do{digits[n++]=char('0'+magnitude%10);magnitude/=10;}while(magnitude);
            if(negative&&used+1<size)out[used]='-',++used;
            while(n){if(used+1<size)out[used]=digits[--n];++used;}
        } else if(*p=='s') {
            const char* text=__builtin_va_arg(args,const char*);
            if(!text)text="(null)";
            while(*text){if(used+1<size)out[used]=*text;++used;++text;}
        } else {
            if(used+1<size)out[used]='%',++used;
            if(used+1<size)out[used]=*p,++used;
        }
    }
    __builtin_va_end(args);
    out[used<size?used:size-1]=0;
    return int(used);
}
}
// libstdc++'s headers call these on container size overflow. This build has no
// exceptions, so they end the process the way an uncaught exception would;
// nothing in the translator should reach them. They say so first: an
// ExitProcess here kills the launcher with no exception record and no result
// line, which is indistinguishable in the log from a fault inside the donor.
namespace std {
static void die(const char* what) {
    OutputDebugStringA("D3D9 translator: aborting on ");
    OutputDebugStringA(what);
    OutputDebugStringA("\n");
    ExitProcess(3);
}
void __throw_length_error(const char*) { die("length_error"); }
void __throw_logic_error(const char*) { die("logic_error"); }
void __throw_bad_alloc() { die("bad_alloc"); }
void __throw_out_of_range_fmt(const char*, ...) { die("out_of_range"); }
void __throw_bad_array_new_length() { die("bad_array_new_length"); }
}
