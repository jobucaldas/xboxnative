// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for ui_nav.h: sidebar <-> grid focus, sidebar view switching,
// sort cycling, queue-row status copy and the game screen's icon-button focus
// rules. Assert-based, no rendering; run through tests/ui-nav.sh.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../ui_nav.h"
#include "../ui_launcher.h"

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using namespace ui_nav;

    // ---- 5-column grid constants (item 4) ----
    {
        check(ui_launcher::kGridCols == 5, "the grid is 5 columns wide");
        check(ui_launcher::kTileW == 232.0f && ui_launcher::kTileH == 348.0f, "tiles are 2:3 (232x348)");
        check(ui_launcher::kGridX + 5 * ui_launcher::kTilePitchX - (ui_launcher::kTilePitchX - ui_launcher::kTileW)
              <= 1844.0f + 0.5f, "5 columns fit the content width (524..1844)");
    }

    // ---- lateral: left from the first grid column reaches the sidebar ----
    {
        Focus f = Focus::grid;
        check(lateral(f, 0, true) && f == Focus::sidebar, "left from column 0 enters the sidebar");
        f = Focus::grid;
        check(!lateral(f, 3, true) && f == Focus::grid, "left inside the grid does nothing");
        f = Focus::sidebar;
        check(lateral(f, 0, false) && f == Focus::grid, "right from the sidebar returns to the grid");
        f = Focus::grid;
        check(!lateral(f, 0, false) && f == Focus::grid, "right inside the grid does nothing");
    }

    // ---- 6-entry sidebar (item 14): Profile, Search, All games, Installed,
    // Download queue, Storage management ----
    {
        check(ui_nav::kSidebarEntries == 6, "the sidebar has 6 entries (profile + search + 4 views)");
        check(ui_nav::kSidebarProfile == 0, "the profile row is entry 0");
        check(ui_nav::kSidebarSearch == 1, "the Search pill is entry 1");
        int a = 1;
        sidebar_move(a, true);  check(a == 2, "sidebar down 1->2 (search -> all games)");
        sidebar_move(a, true);  check(a == 3, "sidebar down 2->3");
        sidebar_move(a, true);  check(a == 4, "sidebar down 3->4");
        sidebar_move(a, true);  check(a == 5, "sidebar down 4->5");
        sidebar_move(a, true);  check(a == 0, "sidebar down wraps 5->0 (storage -> profile)");
        sidebar_move(a, false); check(a == 5, "sidebar up wraps 0->5");
        sidebar_move(a, false); check(a == 4, "sidebar up 5->4");
        // targets: 0 logout sheet, 1 search overlay, 2/3 library, 4 queue, 5 storage
        check(sidebar_target(0) == 0, "the Profile row targets the logout sheet");
        check(sidebar_target(1) == 1, "the Search row targets the search overlay");
        check(sidebar_target(2) == 2 && sidebar_target(3) == 3, "entries 2/3 target the installed/all views");
        check(sidebar_target(4) == 4, "entry 4 opens the download queue");
        check(sidebar_target(5) == 5, "entry 5 opens storage management");
    }

    // ---- sort cycling wraps over the existing three modes ----
    {
        check(cycle_sort(0, 3) == 1, "sort 0->1 (recently played exists)");
        check(cycle_sort(1, 3) == 2, "sort 1->2");
        check(cycle_sort(2, 3) == 0, "sort 2->0 wraps");
        check(cycle_sort(0, 0) == 0, "no modes keeps 0");
    }

    // ---- sort mode constants (item 25): 0 Alphabetical, 1 Most played, 2 Recently played ----
    {
        check(ui_nav::kSortAlphabetical == 0 && ui_nav::kSortMostPlayed == 1 && ui_nav::kSortRecentlyPlayed == 2,
              "sort mode indexes: 0 alphabetical, 1 most played, 2 recently played");
        check(ui_nav::kSortOptionCount == 3, "the sort popup shows three options");
    }

    // ---- storage screen (item 22): drive focus + game list model ----
    {
        // focus clamps into range; an empty list keeps 0 and never moves
        int sel = 0;
        check(storage_drive_clamp(sel, 0) == 0, "no drives: focus stays 0");
        check(!storage_drive_move(sel, 0, true), "no drives: no movement");
        sel = 2;
        check(storage_drive_clamp(sel, 2) == 1, "focus past the last drive clamps onto it");
        sel = -3;
        check(storage_drive_clamp(sel, 2) == 0, "negative focus clamps to the first drive");
        sel = 0;
        check(storage_drive_move(sel, 2, true) && sel == 1, "drive down 0->1");
        check(storage_drive_move(sel, 2, true) && sel == 0, "drive down wraps 1->0");
        check(storage_drive_move(sel, 2, false) && sel == 1, "drive up wraps 0->1");
        check(storage_drive_move(sel, 2, false) && sel == 0, "drive up 1->0");

        // list model: only the installs whose marker names this drive root are
        // listed; empty names fall back to "App <id>"; sizes ride along
        std::vector<std::tuple<uint32_t, std::string, std::string, uint64_t, std::string>> installs = {
            {3405340, "megabonk", "", 123456789, "Megabonk"},        // LocalState (root "")
            {1145360, "hades", "E:\\XboxNativeGames", 999, "Hades"}, // E: drive
            {367520, "hollowknight", "E:\\XboxNativeGames", 555, ""} // E: drive, no cached name
        };
        auto local = storage_game_rows("", installs);
        check(local.size() == 1 && local[0].appid == 3405340 && local[0].name == "Megabonk",
              "the internal drive lists only its own installs");
        auto e = storage_game_rows("E:\\XboxNativeGames", installs);
        check(e.size() == 2 && e[0].name == "Hades" && e[0].bytes == 999, "the drive lists its installs with sizes");
        check(e[1].appid == 367520 && e[1].name == "App 367520", "a game without a cached name reads App <id>");
        auto none = storage_game_rows("F:\\XboxNativeGames", installs);
        check(none.empty(), "a drive with no installs lists nothing (the empty state)");
    }

    // ---- filters state (item 26): the count of enabled filters ----
    {
        bool on[1] = {false};
        check(filters_active(on, 1) == 0, "no filter enabled: count 0");
        on[0] = true;
        check(filters_active(on, 1) == 1, "'Installed only' on: count 1");
        check(filters_active(nullptr, 1) == 0, "no rows: count 0");
    }

    // ---- item 31: the disk widget is sidebar entry 5, reached by Down from
    // the queue row (4); Up from it returns to the queue row ----
    {
        check(ui_nav::kSidebarEntries == 6, "the sidebar has 6 entries");
        check(ui_nav::kSidebarQueue == 4 && ui_nav::kSidebarStorage == 5,
              "queue is row 4, the disk widget is row 5");
        int a = 4;
        sidebar_move(a, true);  check(a == 5, "sidebar down 4->5 (queue -> disk widget)");
        sidebar_move(a, true);  check(a == 0, "sidebar down wraps 5->0 (widget -> profile)");
        sidebar_move(a, false); check(a == 5, "sidebar up wraps 0->5");
        sidebar_move(a, false); check(a == 4, "sidebar up 5->4 (widget -> queue)");
        check(sidebar_target(5) == 5, "the disk widget opens the Storage screen");
    }

    // ---- item 28: the Auto-update row is a real focusable queue row ----
    {
        int sel = 0;
        check(queue_row_clamp(sel, 0) == 0, "no rows: focus stays 0");
        sel = 3;
        check(queue_row_clamp(sel, 2) == 1, "focus past the last row clamps onto it");
        sel = -2;
        check(queue_row_clamp(sel, 2) == 0, "negative focus clamps to the first row");
        sel = 0;
        check(queue_row_move(sel, 3, true) && sel == 1, "queue down 0->1 (auto-update -> first download)");
        check(queue_row_move(sel, 3, true) && sel == 2, "queue down 1->2");
        check(queue_row_move(sel, 3, true) && sel == 0, "queue down wraps 2->0");
        check(queue_row_move(sel, 3, false) && sel == 2, "queue up wraps 0->2");
        check(queue_row_move(sel, 3, false) && sel == 1, "queue up 2->1");
        sel = 0;
        check(!queue_row_move(sel, 0, true), "no rows: no movement");
    }

    // ---- item 32: tab_games applies Playable WITHIN the tab (the caller
    // keeps the tab's own installed-set rule) ----
    {
        struct G { uint32_t appid; };
        std::vector<G> all = {{480}, {3405340}, {1259790}, {220}};
        std::set<uint32_t> installed = {480, 220};
        auto is_play = [](uint32_t a) { return a == 3405340 || a == 1259790; };
        auto tab = [&](bool installed_tab) {
            std::vector<G> t2;
            for (auto& g : all)
                if (!installed_tab || installed.count(g.appid)) t2.push_back(g);
            return t2;
        };
        // All tab, no filter: everything, in order
        auto t = tab_games(tab(false), false, is_play);
        check(t.size() == 4, "All tab without the filter lists every game");
        // All tab, Playable on: only the playable set
        t = tab_games(tab(false), true, is_play);
        check(t.size() == 2 && t[0].appid == 3405340 && t[1].appid == 1259790,
              "All tab with Playable shows only the playable games");
        // Installed tab, no filter: the tab's own list passes through
        t = tab_games(tab(true), false, is_play);
        check(t.size() == 2 && t[0].appid == 480 && t[1].appid == 220,
              "Installed tab without the filter lists the tab's games");
        // Installed tab, Playable on: the intersection (may be empty)
        t = tab_games(tab(true), true, is_play);
        check(t.empty(), "Installed tab with Playable shows the intersection");
    }

    // ---- queue row status copy per download state ----
    {
        check(!std::strcmp(dl_status_text(1, 62, ""), "Downloading"), "state 1 reads Downloading");
        check(!std::strcmp(dl_status_text(2, 100, ""), "Installed"), "state 2 reads Installed");
        check(!std::strcmp(dl_status_text(3, 0, "Cannot reach Steam"), "Cannot reach Steam"),
              "state 3 shows the download message");
        check(!std::strcmp(dl_status_text(3, 0, ""), "Failed"), "state 3 without a message reads Failed");
        check(!std::strcmp(dl_status_text(4, 0, ""), "Cancelled"), "state 4 reads Cancelled");
        check(!std::strcmp(dl_status_text(0, 0, ""), "Waiting"), "idle reads Waiting");
        check(!std::strcmp(dl_status_text(0, 40, ""), "Queued"), "idle with progress reads Queued");
    }

    // ---- game screen focus: slots and icon buttons ----
    {
        check(focus_count(true) == 4 && focus_count(false) == 3, "installed has 4 slots, otherwise 3");
        // installed: trash/cloud/cog are the buttons after the primary, so the
        // slots carry the same numbers as the icon ids (1/2/3)
        check(icon_focus_slot(true, 0) == 1 && icon_focus_slot(true, 1) == 2 && icon_focus_slot(true, 2) == 3,
              "installed icon buttons sit at slots 1..3");
        check(icon_for_slot(true, 0) == 0, "slot 0 is the primary button");
        check(icon_for_slot(true, 1) == 1 && icon_for_slot(true, 2) == 2 && icon_for_slot(true, 3) == 3,
              "installed slots map to trash/cloud/cog");
        // not installed: no trash, so cloud/cog sit at slots 1/2 and carry ids 2/3
        check(icon_focus_slot(false, 0) == 1 && icon_focus_slot(false, 1) == 2,
              "not-installed icon buttons sit at slots 1/2");
        check(icon_for_slot(false, 0) == 0, "slot 0 is the primary button when not installed");
        check(icon_for_slot(false, 1) == 2 && icon_for_slot(false, 2) == 3,
              "not-installed slots map to cloud/cog");
        // The A handler only ever sees slots 0..count-1; out-of-range slots
        // fall back to the primary button rather than a wrong icon.
        check(icon_for_slot(true, 7) == 0 && icon_for_slot(false, 7) == 0, "out-of-range slots fall back to the primary");
        // Walking right across the slots covers exactly the buttons present:
        // every slot maps to a distinct icon id, and the icon set matches the
        // layout (installed: primary+trash+cloud+cog; else primary+cloud+cog).
        for (bool installed : {true, false}) {
            const int count = focus_count(installed);
            int seen[4] = {-1, -1, -1, -1};
            for (int s = 0; s < count; ++s) seen[s] = icon_for_slot(installed, s);
            for (int s = 0; s < count; ++s)
                for (int t = s + 1; t < count; ++t)
                    check(seen[s] != seen[t], "each slot activates a different button");
            check(seen[0] == 0, "slot 0 is always the primary button");
            if (installed) check(seen[1] == 1 && seen[2] == 2 && seen[3] == 3, "installed walk: trash, cloud, cog");
            else check(seen[1] == 2 && seen[2] == 3, "not-installed walk: cloud, cog, no gap");
        }
    }

    // ---- logout reachability (item 14: B on the library root does NOTHING;
    // logout lives on the sidebar's PROFILE row only) ----
    {
        check(!logout_request(false, Focus::grid, 0, 0, true), "B on the grid never opens logout");
        check(!logout_request(false, Focus::sidebar, 0, 0, true), "B in the sidebar does not");
        check(!logout_request(true, Focus::grid, 0, 0, true), "an open confirm sheet does not re-open");
    }

    // ---- destructive sheets default to CANCEL (item 14) ----
    {
        ui_launcher::LogoutVM lo;
        check(!lo.focus_yes, "logout sheet focus defaults to Cancel (never Log out)");
        ui_launcher::DeleteVM d;
        check(!d.focus_delete, "delete sheet focus defaults to Cancel (never Delete)");
    }

    // ---- header controls: focus walk clamps (item 1; review fix 3: 2 controls) ----
    {
        int h = 0;
        check(header_move(h, true) == 1, "header right 0->1 (sort->filters)");
        h = 1;
        check(header_move(h, true) == 1, "header right clamps at filters (no search pill)");
        check(header_move(h, false) == 0, "header left 1->0");
        h = 0; check(header_move(h, false) == 0, "header left clamps at sort");
    }

    // ---- popup exclusivity (review fix 2/5): one popup at a time ----
    {
        bool s = false, f = false, q = false;
        popup_exclusive(s, f, q, 0);
        check(s && !f && !q, "opening the sort popup closes filters and search");
        popup_exclusive(s, f, q, 1);
        check(!s && f && !q, "opening the filters popup closes sort and search");
        popup_exclusive(s, f, q, 2);
        check(!s && !f && q, "opening the search overlay closes sort and filters");
        popup_exclusive(s, f, q, 7);
        check(!s && !f && !q, "an unknown index closes every popup");
    }

    // ---- sort dropdown: move/confirm/close (item 1) ----
    {
        int focus = 1, selected = 0; bool open = true;
        check(sort_open_move(focus, 3, true) && focus == 2, "sort down 1->2");
        check(sort_open_move(focus, 3, true) && focus == 0, "sort down wraps 2->0");
        check(sort_open_move(focus, 3, false) && focus == 2, "sort up wraps 0->2");
        check(sort_confirm(focus, selected, open) && selected == 2 && !open, "A confirms the focused option");
        bool was = sort_close(open);
        check(!was && !open, "B on a closed dropdown is a no-op");
        open = true;
        check(sort_close(open) && !open, "B closes the dropdown");
        int f2 = 0;
        check(!sort_open_move(f2, 0, true), "no options: no movement");
    }

    // ---- filters panel: move + toggle (item 1) ----
    {
        int focus = 0;
        bool on[2] = {false, true};
        check(filters_move(focus, 2, true) && focus == 1, "filters down 0->1");
        check(filters_move(focus, 2, true) && focus == 0, "filters down wraps 1->0");
        check(filters_toggle(on, 2, 0) && on[0], "A toggles the focused row on");
        check(filters_toggle(on, 2, 0) && !on[0], "A toggles it back off");
        check(!filters_toggle(nullptr, 2, 0), "no rows: no toggle");
        check(!filters_toggle(on, 2, 5), "out-of-range focus: no toggle");
    }

    // ---- search overlay keyboard: layout + movement + typing (item 1) ----
    {
        check(kSearchKeyCount == 39, "39 keys: 26 letters, 10 digits, space, backspace, done");
        check(search_key_at(0, 0) == 0 && search_key_at(1, 0) == 10, "row stride is 10 keys");
        check(search_key_at(3, 9) == 38, "the last key is Done");
        check(search_key_at(9, 9) == 38, "out-of-grid lookups clamp to the last key");
        int idx = 0;
        check(search_move(idx, 1, 0) && idx == 1, "right moves one key");
        idx = 9;
        check(search_move(idx, 1, 0) && idx == 0, "right at the row end wraps to its start");
        idx = 0;
        check(search_move(idx, 0, 1) && idx == 10, "down moves one row");
        idx = 25;   // Z: row 2 col 5 -> straight down to row 3 col 5
        check(search_move(idx, 0, 1) && idx == 35, "down from Z keeps the column");
        idx = 29;   // row 2 col 9 -> the last row has 9 keys: clamp onto Done
        check(search_move(idx, 0, 1) && idx == 38, "down from the row-2 end clamps onto Done");
        idx = 35;   // digit 9, row 3 col 5 -> the last row wraps to row 0
        check(search_move(idx, 0, 1) && idx == 5, "down from the last row wraps to the top");
        idx = 38;   // Done, row 3 col 8
        check(search_move(idx, 0, 1) && idx == 8, "down from Done wraps to the top row");
        idx = 0;
        check(search_move(idx, 0, -1) && idx == 30, "up from A wraps to the last row's first key");
        check(!search_move(idx, 0, 0), "no input: no movement");
        // typing
        std::string q; bool open = true;
        check(search_activate(0, q, open) && q == "A", "A types the letter");
        check(search_activate(36, q, open) && q == "A ", "Space appends a space");
        check(search_activate(37, q, open) && q == "A", "Backspace deletes one character");
        check(search_activate(37, q, open) && q.empty() && open, "Backspace on empty keeps the overlay open");
        search_activate(38, q, open);   // Done: closes, query unchanged (returns false)
        check(!open, "Done closes the overlay");
        check(!search_activate(39, q, open), "out-of-range key: no-op");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
