// Off-device CoreWindow-like text events; no physical keyboard or Xbox claim.
#include "feed.h"
#include <cstdio>
#include <vector>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Message {HWND window;UINT message;WPARAM text;DWORD bits;};
static std::vector<Message> messages;
static HWND focused=reinterpret_cast<HWND>(0x1234);
static HWND front=reinterpret_cast<HWND>(0x5678);
static unsigned stateChanges;
static bool acceptPost=true;
static HWND WINAPI focus(){return focused;}
static HWND WINAPI foreground(){return front;}
static void WINAPI sink(int,BOOL){++stateChanges;}
static BOOL WINAPI post(HWND window,UINT message,WPARAM text,LPARAM bits){
    if(!acceptPost){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
    messages.push_back({window,message,text,DWORD(bits)});return TRUE;
}
int main(){
    keyboard_core::Feed feed;feed.focus=focus;feed.foreground=foreground;feed.post=post;feed.sink=sink;
    CHECK(feed.character('A',0x1e,false,false,false,false));
    CHECK(messages.size()==1 && messages[0].window==focused && messages[0].message==WM_CHAR);
    CHECK(messages[0].text=='A' && messages[0].bits==0x001e0001);
    CHECK(feed.character(0xe9,0x12,false,false,true,false,3));
    CHECK(messages.back().text==0xe9 && messages.back().bits==0x40120003);
    // Host layout translation has already happened. Control characters and
    // surrogate halves must arrive unchanged, without virtual-key conversion.
    for(unsigned text:{0u,8u,9u,13u,0x20acu,0xd83du,0xde00u}){
        CHECK(feed.character(text,0,false,false,false,false));
        CHECK(messages.back().text==text && messages.back().message==WM_CHAR);
    }
    CHECK(feed.character('x',0x138,true,true,true,true,0x10000));
    CHECK(messages.back().message==WM_SYSCHAR && messages.back().bits==0xe138ffff);
    CHECK(feed.character('x',0,false,false,false,false,0));
    CHECK(messages.back().bits==1);
    // A text event must not alter virtual-key state or leave modifiers held.
    CHECK(stateChanges==0);
    for(bool down:feed.down)CHECK(!down);
    focused=nullptr;
    CHECK(feed.character('B',0x30,false,false,false,false));
    CHECK(messages.back().window==front);
    auto count=messages.size();
    CHECK(!feed.character(0x10000,0,false,false,false,false));
    CHECK(!feed.character(0xffffffff,0,false,false,false,false));
    CHECK(messages.size()==count);
    front=nullptr;CHECK(!feed.character('C',0,false,false,false,false));
    front=reinterpret_cast<HWND>(0x5678);
    acceptPost=false;SetLastError(0);
    CHECK(!feed.character(0x20ac,0,false,false,false,false));
    CHECK(GetLastError()==ERROR_NOT_SUPPORTED && messages.size()==count);
    feed.post=nullptr;CHECK(!feed.character('D',0,false,false,false,false));
    puts("keyboard text: UTF-16 units, control characters, surrogate order, status bits, targets and queue refusal PASS");
}
