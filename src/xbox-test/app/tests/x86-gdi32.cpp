// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the i686 GDI fixture through the PE32 interpreter with display metrics.
#include <cstdio>
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
    auto result=x86::run_image(file,"x86-gdi32.exe","x86-gdi32.exe",nullptr,nullptr,20'000'000,nullptr,false,nullptr,nullptr,
        [&log](x86::Host& host) {
            host.tee=[&log](const std::string& text) { log += text; };
            host.system_metrics=[](int index,int& value) { if(index==0)value=1280;else if(index==1)value=720;else return false;return true; };
            host.register_window_class=[](const std::u16string& name) { return name==u"X86GDIWindow"; };
            host.create_window=[](const x86::Host::WindowCreate& q)->uintptr_t { return q.class_name==u"X86GDIWindow"&&q.title==u"GDI test"?0x1234u:0; };
            host.destroy_window=[](uintptr_t window) { return window==0x1234u; };
            host.get_dc=[](uintptr_t window)->uintptr_t { return window==0x1234u?0x5678u:0; };
            host.release_dc=[](uintptr_t window,uintptr_t dc) { return window==0x1234u&&dc==0x5678u; };
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
    std::puts("X86-GDI PASS: PE32 GDI routes, user32 unsupported-route contracts, MessageBox logging, DIB guest-memory write, memory-DC BitBlt/StretchBlt readback, and explicit unsupported paths");
    return 0;
}
