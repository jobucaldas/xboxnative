// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the i686 GDI fixture through the PE32 interpreter with display metrics.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../host/x86/x86_play.h"

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: x86-gdi32 <fixture.exe>\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), {}};
    if (file.empty()) { std::fprintf(stderr, "X86-GDI FAIL empty fixture\n"); return 1; }
    std::string log;
    struct Posted { uintptr_t hwnd; uint32_t message,wp,lp,time; int32_t x,y; };
    std::vector<Posted> queue;
    uint32_t fake_style=0;
    auto result=x86::run_image(file,"x86-gdi32.exe","x86-gdi32.exe",nullptr,nullptr,20'000'000,nullptr,false,nullptr,nullptr,
        [&log,&queue,&fake_style](x86::Host& host) {
            host.tee=[&log](const std::string& text) { log += text; };
            host.system_metrics=[](int index,int& value) { if(index==0)value=1280;else if(index==1)value=720;else return false;return true; };
            host.register_window_class=[](const std::u16string& name) { return name==u"X86GDIWindow"; };
            host.create_window=[&fake_style](const x86::Host::WindowCreate& q)->uintptr_t { fake_style=q.style;return q.class_name==u"X86GDIWindow"&&q.title==u"GDI test"?0x1234u:0; };
            host.destroy_window=[](uintptr_t window) { return window==0x1234u; };
            host.get_dc=[](uintptr_t window)->uintptr_t { return window==0x1234u?0x5678u:0; };
            host.release_dc=[](uintptr_t window,uintptr_t dc) { return window==0x1234u&&dc==0x5678u; };
            host.window_call=[&queue,&fake_style](x86::Host::WindowCall& q) {
                auto ok=[&](uintptr_t value=1){q.result=value;q.supported=true;};
                if(q.name=="ShowWindow"){ok(0);}
                else if(q.name=="UpdateWindow"||q.name=="SetWindowPos"||q.name=="MoveWindow"||q.name=="SetWindowTextA"||q.name=="SetWindowTextW"||q.name=="AdjustWindowRect"||q.name=="AdjustWindowRectEx"||q.name=="ClipCursor"){ok();}
                else if(q.name=="GetWindowLongA"||q.name=="GetWindowLongW"){ok(fake_style);}
                else if(q.name=="SetWindowLongA"||q.name=="SetWindowLongW"){uint32_t old=fake_style;fake_style=uint32_t(q.args[2]);ok(old);}
                else if(q.name=="GetClientRect"||q.name=="GetWindowRect"){int32_t rect[4]={0,0,128,72};q.output.resize(sizeof(rect));memcpy(q.output.data(),rect,sizeof(rect));ok();}
                else if(q.name=="ClientToScreen"||q.name=="ScreenToClient"){q.output=q.input;ok();}
                else if(q.name=="GetCursorPos"){int32_t point[2]={3,4};q.output.resize(sizeof(point));memcpy(q.output.data(),point,sizeof(point));ok();}
                else if(q.name=="LoadCursorA"||q.name=="LoadCursorW"||q.name=="LoadIconA"||q.name=="LoadIconW"){ok(0xCAFEu);}
                else if(q.name=="SetCursor"){ok(0);}
                else if(q.name=="ShowCursor"){ok(uint32_t(-1));}
                else if(q.name=="IsWindowVisible"){ok(1);}
                else if(q.name=="IsIconic"){ok(0);}
                else if(q.name=="GetForegroundWindow"){ok(0);}
                else if(q.name=="SetForegroundWindow"){ok(1);}
                else if(q.name=="SetFocus"){ok(0);}
                else if(q.name=="GetFocus"||q.name=="GetActiveWindow"){ok(0x1234u);}
                else if(q.name=="PostMessageA"||q.name=="PostMessageW"){queue.push_back({q.args[0],uint32_t(q.args[1]),uint32_t(q.args[2]),uint32_t(q.args[3]),0,0,0});ok();}
                else if(q.name=="PostQuitMessage"){queue.push_back({0,0x0012u,uint32_t(q.args[0]),0,0,0,0});ok(0);}
                else if(q.name=="PeekMessageA"||q.name=="PeekMessageW"||q.name=="GetMessageA"||q.name=="GetMessageW"){
                    const bool peek=q.name.find("Peek")==0;bool found=!queue.empty();if(found){auto m=queue.front();if(!peek)queue.erase(queue.begin());q.output.resize(32);memcpy(q.output.data(),&m,sizeof(m));}ok(found?1:0);
                }
                else if(q.name=="TranslateMessage"||q.name=="DefWindowProcA"||q.name=="DefWindowProcW"){ok(0);}
                else q.supported=false;
            };
        });
    if (!result.loaded || !result.finished || result.exit_code || !result.unresolved.empty()) {
        std::fprintf(stderr,"X86-GDI FAIL loaded=%d finished=%d exit=%u fault=%s unresolved=%zu\n",result.loaded,result.finished,result.exit_code,result.fault.c_str(),result.unresolved.size());
        return 1;
    }
    if (log.find("x86 MessageBoxA: title: route test\n") == std::string::npos ||
        log.find("x86 MessageBoxW: title: route test\n") == std::string::npos) {
        std::fprintf(stderr,"X86-GDI FAIL MessageBox log missing\n");
        return 1;
    }
    std::puts("X86-GDI PASS: PE32 GDI routes, host-backed user32 windows and message loop (custom WndProc + WM_QUIT), MessageBox logging, DIB guest-memory write, memory-DC BitBlt/StretchBlt readback, and explicit unsupported paths");
    return 0;
}
