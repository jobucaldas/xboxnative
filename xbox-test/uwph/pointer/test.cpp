#include "feed.h"
#include <cstdio>
#include <vector>
#include <limits>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
struct Message {UINT type;WPARAM state;LPARAM position;};
static std::vector<Message> messages;
static POINT cursor{};
static bool held[256]{},acceptPost=true;
static HWND WINAPI foreground(){return HWND(0x1234);}
static BOOL WINAPI rect(HWND,RECT* r){*r={0,0,1920,1080};return TRUE;}
static BOOL WINAPI screen(HWND,POINT* p){p->x+=100;p->y+=200;return TRUE;}
static BOOL WINAPI client(HWND,POINT* p){p->x-=100;p->y-=200;return TRUE;}
static BOOL WINAPI set(int x,int y){cursor={x>1500?1500:x,y};return TRUE;}
static BOOL WINAPI get(POINT* p){*p=cursor;return TRUE;}
static void WINAPI key(int k,BOOL down){held[k]=down;}
static SHORT WINAPI state(int k){return held[k]?SHORT(0x8000):0;}
static BOOL WINAPI post(HWND,UINT m,WPARAM w,LPARAM l){if(!acceptPost){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}messages.push_back({m,w,l});return TRUE;}
int main(){
 pointer_core::Feed feed;feed.foreground=foreground;feed.client_rect=rect;feed.to_screen=screen;
 feed.to_client=client;feed.set_position=set;feed.get_position=get;feed.key=key;feed.post=post;
 CHECK(feed.update(320,180,1280,720,0));
 CHECK(cursor.x==580 && cursor.y==470);
 CHECK(messages.back().type==WM_MOUSEMOVE && messages.back().position==MAKELPARAM(480,270));
 CHECK(feed.update(1280,720,1280,720,MK_LBUTTON|MK_XBUTTON2));
 CHECK(cursor.x==1500 && held[VK_LBUTTON] && held[VK_XBUTTON2]);
 CHECK(messages[messages.size()-2].type==WM_XBUTTONDOWN);
 CHECK(HIWORD(messages[messages.size()-2].state)==XBUTTON2);
 CHECK(messages.back().position==MAKELPARAM(1400,1080));
 CHECK(feed.update(-10,-20,1280,720,0));
 CHECK(!held[VK_LBUTTON] && !held[VK_XBUTTON2]);
 CHECK(messages.back().position==MAKELPARAM(-15,-30));
 auto count=messages.size();
 for(double invalid:{0.,-1.,std::numeric_limits<double>::infinity()})CHECK(!feed.update(1,2,invalid,720,0));
 CHECK(!feed.update(std::numeric_limits<double>::quiet_NaN(),0,1280,720,0));
 CHECK(messages.size()==count);
 acceptPost=false;SetLastError(0);CHECK(!feed.update(1,2,1280,720,MK_RBUTTON));
 CHECK(held[VK_RBUTTON] && GetLastError()==ERROR_NOT_SUPPORTED);
 feed.deactivate();CHECK(!held[VK_RBUTTON] && feed.buttons==0);
 acceptPost=true;feed.key_state=state;held[VK_SHIFT]=held[VK_CONTROL]=true;
 CHECK(feed.update(1,2,1280,720,MK_MBUTTON));
 CHECK(messages.back().state==(MK_MBUTTON|MK_SHIFT|MK_CONTROL));
 CHECK(messages[messages.size()-2].type==WM_MBUTTONDOWN);
 feed.deactivate();CHECK(!held[VK_MBUTTON]);
 feed.foreground=nullptr;CHECK(!feed.update(1,2,1280,720,0));
 puts("pointer: DIP scaling, owner coordinates/clipping, buttons, queue refusal and deactivation PASS");
}
