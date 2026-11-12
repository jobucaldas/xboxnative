# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual detail-render expression without the Windows UI dependencies."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


class StoreBadgeTest(unittest.TestCase):
    def test_only_reads_published_store_fields(self):
        source = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text()
        detail = source.split('void draw_detail(double t) {', 1)[1]
        snapshot = re.search(r'auto si = m_dSi; int st = [^;]+;', detail).group()
        badge = re.search(r'apib::Result gfx = apib::combine\([^;]+;', detail).group()
        program = r'''
#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <thread>
struct Note {
    std::string text;
    std::atomic<int>* state;
    operator std::string() const {
        // Deterministically reject reading even an empty unpublished string.
        assert(state->load() == 1);
        return text;
    }
};
struct StoreInfo {
    std::atomic<int> state{0};
    int dxKind = 0;
    Note dxNote{{}, &state};
};
namespace apib {
struct Result { int kind = 0; std::string note; };
Result lookup(unsigned) { return {}; }
Result combine(Result r, int kind, const std::string& note) {
    r.kind = kind; r.note = note; return r;
}
}
apib::Result draw(const std::shared_ptr<StoreInfo>& m_dSi) {
    unsigned m_dApp = 0;
    SNAPSHOT
    BADGE
    return gfx;
}
int main() {
    assert(draw(nullptr).note.empty());
    auto si = std::make_shared<StoreInfo>();
    for (int state : {0, 2}) {
        si->state = state;
        auto r = draw(si);
        assert(r.kind == 0 && r.note.empty());
    }
    si->state = 0;
    std::atomic<bool> begin{false}, writing{false}, finish{false};
    std::thread worker([&] {
        while (!begin.load()) std::this_thread::yield();
        // Mutate the unpublished string while detail frames are rendered.
        do {
            si->dxNote.text.assign(4096, 'x');
            si->dxNote.text.clear();
            writing = true;
        } while (!finish.load());
        si->dxKind = 2;
        si->dxNote.text = "DirectX 11";
        si->state = 1; // Same release/acquire publication as StoreInfo.
    });
    begin = true;
    while (!writing.load()) std::this_thread::yield();
    for (int i = 0; i < 10000; ++i) {
        auto r = draw(si);
        assert(r.kind == 0 && r.note.empty());
    }
    finish = true;
    while (si->state.load() != 1) std::this_thread::yield();
    for (int i = 0; i < 10000; ++i) {
        auto r = draw(si);
        assert(r.kind == 2 && r.note == "DirectX 11");
    }
    worker.join();
}
'''.replace('SNAPSHOT', snapshot).replace('BADGE', badge)
        with tempfile.TemporaryDirectory(prefix='store-badge-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = Path(temp) / 'test'
            cpp.write_text(program)
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=30)


class StoreWorkerCopyTest(unittest.TestCase):
    def test_worker_copies_every_parsed_field_onto_the_live_object(self):
        """store_parse fills a temporary; store_worker must publish all of it.

        The game page's size row falls back to StoreInfo::download_size, so a
        field the worker forgets to copy reads as empty and the page shows
        Unknown instead of the store's Storage: figure.
        """
        source = (Path(__file__).resolve().parents[1] / 'store.h').read_text()
        copy = re.search(r'if \(ok\) \{ si->name = tmp\.name;[^}]*\}', source).group()
        program = r'''
#include <cassert>
#include <string>
struct StoreInfo {
    std::string name, desc, release, dev, genres, price;
    std::string download_size;
    bool free = false;
    int dxKind = 0;
    std::string dxNote;
};
int main() {
    const bool ok = true;
    StoreInfo tmp;
    tmp.name = "Halo Infinite";
    tmp.desc = "A description";
    tmp.release = "Nov 15, 2021";
    tmp.dev = "343 Industries";
    tmp.genres = "Action";
    tmp.price = "Free to play";
    tmp.free = true;
    tmp.dxKind = 2;
    tmp.dxNote = "DirectX 12";
    tmp.download_size = "30 GB";
    StoreInfo live;
    StoreInfo* si = &live;
    COPY
    assert(si->name == "Halo Infinite");
    assert(si->desc == "A description");
    assert(si->release == "Nov 15, 2021");
    assert(si->dev == "343 Industries");
    assert(si->genres == "Action");
    assert(si->price == "Free to play");
    assert(si->free);
    assert(si->dxKind == 2);
    assert(si->dxNote == "DirectX 12");
    assert(si->download_size == "30 GB");   // the size row's fallback
}
'''.replace('COPY', copy)
        with tempfile.TemporaryDirectory(prefix='store-copy-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = Path(temp) / 'test'
            cpp.write_text(program)
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
