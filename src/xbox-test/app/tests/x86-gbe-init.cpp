// SPDX-License-Identifier: GPL-3.0-or-later
// Run the pinned 32-bit gbe provider as a guest dependency and report the first
// unresolved import or interpreter fault without loading the DLL in the host OS.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include "../host/x86/x86_play.h"
using namespace x86;

static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::printf("FAIL cannot open %s\n", path); std::exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}

struct TestFile { std::string bytes; size_t position = 0; };
static void configure_host(Host& host) {
    auto files = std::make_shared<std::map<uint32_t, TestFile>>();
    auto next_handle = std::make_shared<uint32_t>(1);
    auto content_for = [](const std::string& path, std::string& contents) {
        std::string normalized = path;
        for (char& ch : normalized) if (ch == '\\') ch = '/';
        std::string local;
        if (normalized.size() >= 15 && normalized.compare(normalized.size() - 15, 15, "steam_appid.txt") == 0) local = "steam_appid.txt";
        else if (normalized.size() >= 31 && normalized.compare(normalized.size() - 31, 31, "steam_settings/configs.main.ini") == 0) local = "steam_settings/configs.main.ini";
        else return false;
        std::ifstream f(local, std::ios::binary);
        if (!f) return false;
        contents.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    host.file_call = [files, next_handle, content_for](Host::FileCall& q) {
        if (q.kind == Host::FileCall::Kind::GetCurrentDirectoryW) {
            const std::u16string cwd = u"C:\\x86-gbe-init";
            q.data.resize((cwd.size() + 1) * 2);
            for (size_t i = 0; i < cwd.size(); ++i) { q.data[i * 2] = uint8_t(cwd[i]); q.data[i * 2 + 1] = uint8_t(cwd[i] >> 8); }
            q.value = cwd.size(); q.succeeded = true; return;
        }
        if (q.kind == Host::FileCall::Kind::AttributesA) {
            std::string contents;
            if (!content_for(q.path_a, contents)) { q.error = 2; return; }
            q.value = 0x20; q.succeeded = true;
            if (q.args[1]) {
                q.data.assign(36, 0);
                uint32_t size = uint32_t(contents.size());
                q.data[0] = 0x20; q.data[28] = uint8_t(size); q.data[29] = uint8_t(size >> 8);
                q.data[30] = uint8_t(size >> 16); q.data[31] = uint8_t(size >> 24);
            }
            return;
        }
        if (q.kind == Host::FileCall::Kind::CreateW) {
            std::string path(q.path_w.begin(), q.path_w.end()), contents;
            if (!content_for(path, contents)) { q.error = 2; return; }
            uint32_t handle = (*next_handle)++;
            (*files)[handle] = {contents, 0}; q.value = handle; q.succeeded = true; return;
        }
        if (q.kind == Host::FileCall::Kind::Read) {
            auto it = files->find(uint32_t(q.handle));
            if (it == files->end()) { q.error = 6; return; }
            size_t count = std::min<size_t>(q.args[0], it->second.bytes.size() - std::min(it->second.position, it->second.bytes.size()));
            q.data.assign(it->second.bytes.begin() + it->second.position, it->second.bytes.begin() + it->second.position + count);
            q.args[1] = uint32_t(count); it->second.position += count; q.succeeded = true; return;
        }
        if (q.kind == Host::FileCall::Kind::Size) {
            auto it = files->find(uint32_t(q.handle));
            if (it == files->end()) { q.error = 6; return; }
            q.value = it->second.bytes.size(); q.args[1] = uint32_t(q.value >> 32); q.succeeded = true; return;
        }
        if (q.kind == Host::FileCall::Kind::Seek || q.kind == Host::FileCall::Kind::SeekEx) {
            auto it = files->find(uint32_t(q.handle));
            if (it == files->end()) { q.error = 6; return; }
            int64_t distance = q.kind == Host::FileCall::Kind::SeekEx ? q.distance : int32_t(q.args[0]);
            uint32_t method = q.kind == Host::FileCall::Kind::SeekEx ? q.method : q.args[2];
            int64_t base = method == 0 ? 0 : method == 1 ? int64_t(it->second.position) : int64_t(it->second.bytes.size());
            int64_t position = base + distance;
            if (method > 2 || position < 0) { q.error = 87; return; }
            it->second.position = size_t(position); q.position = position; q.value = uint32_t(position); q.args[1] = uint32_t(uint64_t(position) >> 32); q.succeeded = true; return;
        }
        if (q.kind == Host::FileCall::Kind::Close) {
            q.succeeded = files->erase(uint32_t(q.handle)) != 0;
            if (!q.succeeded) q.error = 6;
        }
    };
}

int main(int argc, char** argv) {
    if (argc != 3) { std::puts("usage: x86-gbe-init <fixture.exe> <steam_api.dll>"); return 2; }
    auto fixture = slurp(argv[1]);
    auto provider = slurp(argv[2]);
    DllSource source = [provider](const std::string& name) {
        return name == "steam_api.dll" ? provider : std::vector<uint8_t>{};
    };
    std::string output;
    auto tee = [&output](const std::string& text) { output += text; std::fwrite(text.data(), 1, text.size(), stdout); };
    PlayResult r = run_image(fixture, "x86-gbe-init.exe", "x86-gbe-init.exe", tee,
                             nullptr, 500000000ull, source, true, nullptr, nullptr, configure_host);
    std::printf("play: loaded=%d finished=%d exit=%u imports=%u bound=%u\n",
                r.loaded ? 1 : 0, r.finished ? 1 : 0, r.exit_code, r.imports, r.bound);
    for (const auto& name : r.unresolved) std::printf("play: unresolved %s\n", name.c_str());
    if (!r.fault.empty()) std::printf("play: fault kind=%s eip=0x%08x %s\n",
                                      r.fault_kind.c_str(), r.fault_eip, r.fault.c_str());
    if (!r.loaded || !r.finished || r.exit_code != 42 || !r.unresolved.empty() || !r.fault.empty()) {
        std::puts("X86-GBE-INIT FAIL");
        return 1;
    }
    std::puts("fixture: SteamAPI_Init=1 RunCallbacks=returned GetSteamID=nonzero BIsSubscribedApp(480)=1");
    std::puts("X86-GBE-INIT PASS");
    return 0;
}
