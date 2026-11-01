#include "feed.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static std::array<bool,256> observed{};
static unsigned calls,posts;
static UINT message;static WPARAM key;static DWORD bits;
static HWND target=reinterpret_cast<HWND>(0x1234);
static HWND WINAPI focus(){return target;}
static HWND WINAPI foreground(){return reinterpret_cast<HWND>(0x5678);}
static BOOL WINAPI post(HWND h,UINT m,WPARAM w,LPARAM l){
    if(h!=(target?target:foreground()))return FALSE;
    ++posts;message=m;key=w;bits=DWORD(l);return TRUE;
}
static void WINAPI sink(int key,BOOL down){observed[key]=!!down;++calls;}
int main() {
    keyboard_core::Feed feed;feed.sink=sink;
    feed.key('A',0x1e,false,true);CHECK(observed['A']);
    feed.key('A',0x1e,false,false);CHECK(!observed['A']);
    feed.key(VK_SHIFT,0x2a,false,true);CHECK(observed[VK_SHIFT] && observed[VK_LSHIFT] && !observed[VK_RSHIFT]);
    feed.key(VK_SHIFT,0x36,false,true);CHECK(observed[VK_RSHIFT]);
    feed.key(VK_SHIFT,0x2a,false,false);CHECK(observed[VK_SHIFT] && !observed[VK_LSHIFT]);
    feed.key(VK_SHIFT,0x36,false,false);CHECK(!observed[VK_SHIFT]);
    feed.key(VK_CONTROL,0x1d,true,true);CHECK(observed[VK_RCONTROL] && observed[VK_CONTROL]);
    feed.key(VK_MENU,0x38,false,true);CHECK(observed[VK_LMENU] && observed[VK_MENU]);
    feed.key('W',0x11,false,true);feed.deactivate();
    for(bool down:observed)CHECK(!down);
    auto count=calls;feed.key(256,0,false,true);CHECK(calls==count);
    feed.sink=nullptr;feed.key('A',0,false,true);feed.deactivate();
    feed.sink=sink;feed.focus=focus;feed.foreground=foreground;feed.post=post;
    feed.key('A',0x1e,false,true);
    CHECK(posts==1 && message==WM_KEYDOWN && key=='A' && bits==0x001e0001);
    feed.key('A',0x1e,false,true,2);
    CHECK(posts==2 && bits==0x401e0002);
    feed.key('A',0x1e,false,false);
    CHECK(message==WM_KEYUP && bits==0xc01e0001);
    feed.key(VK_CONTROL,0x1d,true,true);
    CHECK(message==WM_KEYDOWN && key==VK_CONTROL && bits==0x011d0001);
    feed.key(VK_MENU,0x38,false,true);
    CHECK(message==WM_SYSKEYDOWN && key==VK_MENU && bits==0x20380001);
    feed.key('F',0x21,false,true);
    CHECK(message==WM_SYSKEYDOWN && key=='F' && bits==0x20210001);
    feed.key(VK_MENU,0x38,false,false);
    CHECK(message==WM_SYSKEYUP && bits==0xe0380001);
    feed.key(VK_F10,0x44,false,true);
    CHECK(message==WM_SYSKEYDOWN && bits==0x00440001);
    count=posts;feed.deactivate();CHECK(posts==count);
    target=nullptr;feed.key('W',0x11,false,true);
    CHECK(posts==count+1 && message==WM_KEYDOWN);
    feed.foreground=nullptr;count=posts;feed.key('W',0x11,false,false);CHECK(posts==count);
    feed.post=nullptr;feed.key('A',0x1e,false,true);feed.deactivate();
    puts("keyboard feed: real-event key state, modifier sides/aggregation, focus loss, key message flags/repeats/targets and bounds PASS");
}
