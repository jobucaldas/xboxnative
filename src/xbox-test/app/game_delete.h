// SPDX-License-Identifier: GPL-3.0-or-later
// Delete an installed game from the detail page (X). Refused while that game
// downloads or after any game was started in this process (its files may be
// mapped). The removal itself (installs.h) runs on a worker thread. Saves and
// Steam provider settings (LocalState\steam\<appid>) are never touched.
#pragma once
#include <set>
#include "installs.h"

struct DelState {
    std::atomic<int> state{0};          // 0 idle, 1 running, 2 done, 3 finished with leftovers
    std::atomic<uint32_t> appid{0};
    std::atomic<bool> keptShared{false}, untouched{false};   // untouched: marker could not be removed, nothing deleted
    std::atomic<int> gen{0};            // bumped when a delete finishes
    installs::Progress progress;
};
static DelState g_del;

static std::set<uint32_t> installed_appids() { std::set<uint32_t> s; for (auto& i : installs::scan(g_dir, false)) s.insert(i.first); return s; }

// UI thread. Returns "" when the delete started, else the reason it cannot.
static std::string start_delete(uint32_t appid) {
    if (g_del.state == 1) return "Another game is being deleted.";
    if (play_runtime::observed.load(std::memory_order_acquire) != nullptr || g_launch.phase == 1 || g_launch.phase == 3)
        return "A game was started: reopen the app to delete.";
    if ((g_dl.appid == appid && g_dl.state == 1) || is_queued(appid)) return "Downloading: cancel the download first.";
    InstallInfo ii = read_install(appid);
    if (!ii.installed) return "This game is not installed.";
    bool shared = game_library::installdir_shared(appid, ii.dir, installs::scan(g_dir, true));
    if (!shared && !game_library::deletable_installdir(ii.dir)) { logf("delete: appid %u refused, install folder not deletable", appid); return "This install folder cannot be deleted here."; }
    if (g_dl.appid == appid) g_dl.state = 0;   // drop the finished/failed download overlay for this game
    g_del.appid = appid; g_del.progress.removed = 0; g_del.progress.failed = 0; g_del.keptShared = shared; g_del.untouched = false; g_del.state = 1;
    logf("delete: start appid %u folder LocalState\\%s (%s)", appid, ii.dir.c_str(), shared ? "shared with another install: marker only" : "marker + folder");
    std::thread([appid, dir = ii.dir, shared] {
        double t0 = now_s();
        bool ok = installs::remove_install(g_dir, appid, dir, shared, g_del.progress, &g_quit);
        unsigned failed = g_del.progress.failed;
        g_del.untouched = !ok;
        if (!ok) logf("delete: appid %u marker could not be removed; nothing deleted", appid);
        else logf("delete: appid %u done, %llu files removed, %u left behind, %.1f s", appid, (unsigned long long)g_del.progress.removed.load(), failed, now_s() - t0);
        g_del.state = failed ? 3 : 2; g_del.gen++;
    }).detach();
    return "";
}
