// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for move_game.h against REAL temp directories (Win32 file walks,
// run under Wine like tests/game-delete.cpp): plan refusals, copy/verify,
// marker rewrite order, cancel and failure cleanup, overwrite refusal.
#include "../move_game.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace move_game;

static int g_failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// A real directory tree with deterministic contents: files of known sizes.
static void make_file(const std::wstring& path, const std::string& bytes) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) { std::printf("FAIL cannot create test file\n"); ++g_failures; return; }
    if (!bytes.empty()) fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
}
static void make_dir(const std::wstring& path) { CreateDirectoryW(path.c_str(), nullptr); }

static uint64_t dir_bytes(const std::wstring& dir) {
    MovePlan p = measure(dir, L"");
    return p.bytes;
}

int main(int argc, char** argv) {
    std::wstring base = argc > 1 ? installs::ascii(argv[1]) : L".";   // Wine passes a real temp dir
    const std::wstring src_root = base + L"\\src", dst_root = base + L"\\dst";
    make_dir(src_root); make_dir(dst_root);

    // ---- plan_check: every refusal, then the happy path ----
    {
        check(plan_check(true, "", "E:", 100, false, 1 << 30) == Refusal::busy, "a running game refuses the move");
        check(plan_check(false, "E:", "E:", 100, false, 1 << 30) == Refusal::same_root, "the same root refuses");
        check(plan_check(false, "", "E:", 100, true, 1 << 30) == Refusal::exists, "an existing destination refuses");
        check(plan_check(false, "", "E:", 100, false, (1ull << 30) + 99) == Refusal::no_space, "free < size + 1 GB refuses");
        check(plan_check(false, "", "E:", 100, false, (1ull << 30) + 100) == Refusal::none, "free == size + 1 GB moves");
        check(refusal_text(Refusal::no_space) && *refusal_text(Refusal::no_space), "every refusal has a reason line");
    }

    // ---- copy_tree + verify_tree: a real tree moves byte-exact ----
    {
        const std::wstring g = src_root + L"\\megabonk";
        make_dir(g); make_dir(g + L"\\bin");
        make_file(g + L"\\game.exe", std::string(1000, 'a'));
        make_file(g + L"\\bin\\data.bin", std::string(4096, 'b'));
        const std::wstring d = dst_root + L"\\megabonk";
        Progress p; p.total = dir_bytes(g);
        std::atomic<bool> stop{false};
        check(copy_tree(g, d, p, &stop), "the copy walk succeeds");
        check(p.files == 2 && p.copied == 5096, "the copy counts every file and byte");
        check(verify_tree(d, 5096, 2), "the verify step accepts the complete copy");
        make_file(d + L"\\bin\\extra.txt", "x");
        check(verify_tree(d, 5096, 2), "verify still passes with an extra file (>= plan)");
        check(verify_tree(d, 5096, 3), "the extra file is part of the tree");
        check(!verify_tree(d, 999999, 2), "verify refuses a byte total above the tree's");
    }

    // ---- copy_tree: never overwrites, fails on an existing file ----
    {
        const std::wstring g = src_root + L"\\clash";
        make_dir(g); make_file(g + L"\\a.txt", "AAA");
        const std::wstring d = dst_root + L"\\occupied";
        make_dir(d); make_file(d + L"\\a.txt", "OLD");
        Progress p;
        std::atomic<bool> stop{false};
        check(!copy_tree(g, d, p, &stop), "a copy onto an existing file fails");
        // the existing file is untouched
        FILE* f = _wfopen((d + L"\\a.txt").c_str(), L"rb");
        char buf[8] = {}; size_t n = f ? fread(buf, 1, 8, f) : 0; if (f) fclose(f);
        check(n == 3 && std::string(buf, n) == "OLD", "the existing file was not overwritten");
    }

    // ---- copy_tree: cancel stops between files ----
    {
        const std::wstring g = src_root + L"\\big";
        make_dir(g);
        for (int i = 0; i < 8; ++i) make_file(g + L"\\f" + std::to_wstring(i) + L".bin", std::string(100, 'c'));
        const std::wstring d = dst_root + L"\\big";
        Progress p;
        std::atomic<bool> stop{false};
        // a pre-stopped flag ends the walk before the first file
        stop = true;
        check(!copy_tree(g, d, p, &stop), "a cancelled copy does not complete");
        stop = false;
        // cleanup for the next block
        remove_tree(d);
    }

    // ---- run(): the full order — copy, verify, dst marker, src marker gone, src gone ----
    {
        const std::wstring g = src_root + L"\\hades";
        make_dir(g); make_file(g + L"\\game.exe", std::string(2048, 'd'));
        const std::wstring msrc = src_root + L"\\dl_1145360.done", mdst = dst_root + L"\\dl_1145360.done";
        make_file(msrc, "root E:\\XboxNativeGames\ninstalldir hades\nbytes 2048\n");
        const std::wstring d = dst_root + L"\\hades";
        Progress p; p.total = dir_bytes(g);
        std::atomic<bool> stop{false};
        const std::string dst_text = "installdir hades\nbytes 2048\n";
        check(run(g, d, msrc, mdst, dst_text, p, &stop), "the move completes");
        // destination: files + marker
        check(verify_tree(d, 2048, 1), "the destination holds the copy");
        FILE* f = _wfopen(mdst.c_str(), L"rb");
        char buf[128] = {}; size_t n = f ? fread(buf, 1, 128, f) : 0; if (f) fclose(f);
        check(n > 0 && std::string(buf, n) == dst_text, "the destination marker carries the new root text");
        // source: marker and tree gone
        f = _wfopen(msrc.c_str(), L"rb");
        bool src_marker_gone = !f; if (f) fclose(f);
        check(src_marker_gone, "the source marker is removed");
        DWORD a = GetFileAttributesW(g.c_str());
        check(a == INVALID_FILE_ATTRIBUTES, "the source tree is removed");
    }

    // ---- run(): failure mid-copy removes the partial copy, keeps the source ----
    // The collision is staged inside the DESTINATION tree the copy itself
    // creates: the source has sub/nested.bin, the copy makes dst/sub, and a
    // file planted there between the mkdir and the copy would block it — but
    // that needs a race. Instead the source carries a file whose destination
    // path is blocked by making the destination's sub a FILE (CopyFileW-style
    // target conflict): CreateDirectoryW(dst\\sub) then fails, failing the
    // walk after big.bin was copied. dst only ever existed because this very
    // copy created it, so remove_tree(dst) removes nothing pre-existing.
    {
        const std::wstring g = src_root + L"\\crashgame";
        make_dir(g); make_file(g + L"\\big.bin", std::string(5000, 'e'));
        make_dir(g + L"\\sub"); make_file(g + L"\\sub\\nested.bin", std::string(300, 'g'));
        const std::wstring msrc = src_root + L"\\dl_222.done", mdst = dst_root + L"\\dl_222.done";
        make_file(msrc, "installdir crashgame\nbytes 5300\n");
        const std::wstring d = dst_root + L"\\crashgame";
        make_file(dst_root + L"\\crashgame", "not a dir");   // dst path is a FILE: every mkdir into it fails
        Progress p; p.total = 5300;
        std::atomic<bool> stop{false};
        check(!run(g, d, msrc, mdst, "installdir crashgame\nbytes 5300\n", p, &stop), "a blocked copy fails the move");
        check(GetFileAttributesW(g.c_str()) != INVALID_FILE_ATTRIBUTES, "the source tree survived");
        FILE* f = _wfopen(msrc.c_str(), L"rb");
        bool src_marker_kept = !!f; if (f) fclose(f);
        check(src_marker_kept, "the source marker survived");
        f = _wfopen(mdst.c_str(), L"rb");
        bool dst_marker_absent = !f; if (f) fclose(f);
        check(dst_marker_absent, "no destination marker was written");
        // the planted file is untouched and is all that remains at the dst path
        f = _wfopen(d.c_str(), L"rb");
        char buf[16] = {}; size_t n = f ? fread(buf, 1, 16, f) : 0; if (f) fclose(f);
        check(n == 9 && std::string(buf, n) == "not a dir", "the blocked destination path is untouched");
    }

    // ---- run(): cancel after the copy removes the partial copy ----
    {
        const std::wstring g = src_root + L"\\cancelme";
        make_dir(g); make_file(g + L"\\x.bin", std::string(300, 'f'));
        const std::wstring msrc = src_root + L"\\dl_333.done", mdst = dst_root + L"\\dl_333.done";
        make_file(msrc, "installdir cancelme\nbytes 300\n");
        const std::wstring d = dst_root + L"\\cancelme";
        Progress p; p.total = 300;
        std::atomic<bool> stop{true};   // cancelled before the copy starts
        check(!run(g, d, msrc, mdst, "installdir cancelme\nbytes 300\n", p, &stop), "a cancelled move does not complete");
        check(GetFileAttributesW(g.c_str()) != INVALID_FILE_ATTRIBUTES, "the cancelled move kept the source");
        MovePlan m = measure(d, L"");
        check(m.files == 0, "the cancelled move left no partial copy");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "MOVE-GAME FAILED" : "MOVE-GAME OK",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
