// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for ui_unlocks.h (the guide's achievement glue, pure parts):
// unlock->view mapping, earned-set diffs for the unlock poller, schema-index
// lookup and the Steam CDN icon URL; plus a fake achievements.json round-trip
// through steam_achievements::parse_earned (the poller's re-parse path).
// Assert-based, no rendering; run through tests/ui-unlocks.sh.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "../ui_unlocks.h"

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

steam_achievements::Achievement ach(const char* name, const char* display, const char* desc,
                                    const char* icon, const char* gray, bool hidden) {
    steam_achievements::Achievement a;
    a.block = 0; a.bit = 0;
    a.name = name; a.display = display; a.description = desc;
    a.icon = icon; a.iconGray = gray; a.hidden = hidden;
    return a;
}

}  // namespace

int main() {
    using namespace ui_unlocks;
    using steam_achievements::Earned;

    // ---- unlock -> view mapping ----
    {
        auto a = ach("ACH_WIN", "Get a Win", "Win one match", "iconhash", "grayhash", false);
        // locked, never earned
        ui_guide::AchievementView v = to_view(a, Earned{});
        check(!v.unlocked && !v.hidden && v.earned.empty(), "locked view: not unlocked, no date");
        check(v.name == "Get a Win" && v.description == "Win one match", "locked view: display name and description");
        // earned now
        v = to_view(a, Earned{true, 1794225600u});
        check(v.unlocked && v.earned == "Nov 9, 2026", "earned view: unlocked with the UTC date");
        // hidden while locked shows the masked texts; unlocked shows the real ones
        auto h = ach("ACH_SECRET", "Real Name", "Real description", "", "", true);
        v = to_view(h, Earned{});
        check(v.name == "Hidden achievement" && v.description == "Details are hidden until unlocked",
              "hidden+locked view: masked texts");
        v = to_view(h, Earned{true, 0});
        check(v.name == "Real Name" && v.description == "Real description" && v.unlocked,
              "hidden+earned view: real texts revealed");
        // empty display falls back to the internal name
        v = to_view(ach("ACH_RAW", "", "desc", "", "", false), Earned{});
        check(v.name == "ACH_RAW", "empty display falls back to the internal name");
    }

    // ---- guide list from schema + earned map ----
    {
        steam_achievements::Schema s;
        s.achievements = {ach("A1", "First", "d1", "", "", false),
                          ach("A2", "Second", "d2", "", "", true),
                          ach("A3", "Third", "d3", "", "", false)};
        std::map<std::string, Earned> earned{{"A1", {true, 1767225600u}}, {"A3", {true, 1794225600u}}};
        auto views = build_views(s, earned);
        check(views.size() == 3, "build_views: one row per schema achievement");
        check(views[0].unlocked && views[0].earned == "Jan 1, 2026", "build_views: A1 unlocked");
        check(views[1].name == "Hidden achievement" && !views[1].unlocked, "build_views: A2 hidden and locked");
        check(views[2].unlocked, "build_views: A3 unlocked");
        check(int(views.size()) == 3, "build_views order matches schema order");
    }

    // ---- earned-set diff (the unlock poller's core) ----
    {
        std::map<std::string, Earned> before{{"A1", {true, 100u}}, {"A2", {false, 0}}};
        std::map<std::string, Earned> now{{"A1", {true, 100u}}, {"A2", {true, 555u}}, {"A3", {true, 556u}}};
        auto fresh = newly_earned(before, now);
        check(fresh.size() == 2 && fresh[0] == "A2" && fresh[1] == "A3",
              "newly_earned: A2 unearned->earned and the new A3, not A1");
        check(newly_earned(now, now).empty(), "newly_earned: no diff against itself");
        check(newly_earned(before, before).empty(), "newly_earned: no diff when nothing changed");
        // a timestamp-only change is not a new unlock
        std::map<std::string, Earned> retimed{{"A1", {true, 999u}}, {"A2", {false, 0}}};
        check(newly_earned(before, retimed).empty(), "newly_earned: earned_time drift alone is not an unlock");
    }

    // ---- schema index lookup (toast -> guide row) ----
    {
        steam_achievements::Schema s;
        s.achievements = {ach("A1", "First", "d1", "", "", false), ach("A2", "Second", "d2", "", "", false)};
        std::vector<ui_guide::AchievementView> views(2);
        views[0].name = "First"; views[1].name = "Second";
        check(index_of(s, views, "A2") == 1, "index_of finds the schema name's row");
        check(index_of(s, views, "nope") == -1, "index_of returns -1 for an unknown name");
    }

    // ---- icon URL (public CDN, no account data) ----
    {
        check(icon_url(3405340, "abc123") ==
                  "https://cdn.fastly.steamstatic.com/steamcommunity/public/images/apps/3405340/abc123.jpg",
              "icon_url: CDN pattern with appid and hash");
        check(icon_url(1, "").empty(), "icon_url: empty hash gives an empty URL");
        // item D20: the schema's icon value already ends in .jpg — appending
        // another one 404'd every achievement icon on the console.
        check(icon_url(3405340, "4c378fd9006af550a537466bc7a9ff876c05ef4c.jpg") ==
                  "https://cdn.fastly.steamstatic.com/steamcommunity/public/images/apps/3405340/"
                  "4c378fd9006af550a537466bc7a9ff876c05ef4c.jpg",
              "D20: a hash that already carries .jpg is not suffixed twice");
    }

    // ---- fake achievements.json: the poller's parse -> diff -> view round-trip ----
    {
        const char* json =
            "{\n"
            "  \"ACH_HIDDEN_ONE\": {\"earned\": false, \"earned_time\": 0},\n"
            "  \"ACH_FIRST\": {\"earned\": true, \"earned_time\": 1794225600},\n"
            "  \"progress_stat\": 7\n"   // non-object values are skipped
            "}\n";
        auto parsed = steam_achievements::parse_earned(json);
        check(parsed.size() == 3, "parse_earned: both achievements plus the bare number (default entry)");
        check(parsed["progress_stat"].earned == false, "parse_earned: bare scalar stays unearned");
        check(parsed["ACH_FIRST"].earned && parsed["ACH_FIRST"].time == 1794225600u,
              "parse_earned: earned flag and time");
        check(!parsed["ACH_HIDDEN_ONE"].earned, "parse_earned: not-yet-earned entry");

        steam_achievements::Schema s;
        s.achievements = {ach("ACH_HIDDEN_ONE", "Hidden One", "d", "h1", "g1", true),
                          ach("ACH_FIRST", "First Steps", "d", "h2", "g2", false)};
        auto views = build_views(s, parsed);
        check(views.size() == 2 && views[0].name == "Hidden achievement" && views[1].unlocked,
              "fake file -> views: hidden masked, earned unmasked");

        // the game unlocks ACH_HIDDEN_ONE while playing: parse again, diff, map
        auto later = steam_achievements::parse_earned(
            "{\"ACH_HIDDEN_ONE\": {\"earned\": true, \"earned_time\": 1794312000},"
            " \"ACH_FIRST\": {\"earned\": true, \"earned_time\": 1794225600}}");
        auto fresh = newly_earned(parsed, later);
        check(fresh.size() == 1 && fresh[0] == "ACH_HIDDEN_ONE", "poll diff: the new unlock is the only new one");
        auto views2 = build_views(s, later);
        const int idx = index_of(s, views2, "ACH_HIDDEN_ONE");
        check(idx == 0 && views2[0].unlocked && views2[0].name == "Hidden One" && views2[0].earned == "Nov 10, 2026",
              "after unlock: hidden achievement revealed with its earned date");
    }

    // ---- guide store end-to-end: set views, push an unlock, read the toast ----
    {
        ui_guide::AchievementStore store;
        steam_achievements::Schema s;
        s.achievements = {ach("A1", "First", "d1", "", "", false), ach("A2", "Second", "d2", "", "", true)};
        store.set_achievements(build_views(s, {}));
        check(store.total() == 2 && store.unlocked() == 0, "store: two locked rows");
        ui_guide::Toast t = store.push_unlock(1, "Nov 10, 2026", 99.0);
        check(t.title == "Hidden achievement" && t.born == 99.0, "push_unlock: toast carries the visible name");
        check(store.unlocked() == 1, "push_unlock: the row counts as unlocked");
        // expiry on an injected clock: a toast stamped at 99.0 dies at 104+ on
        // the same clock (guest_overlay stamps born with its own QPC seconds
        // and drops with the same ui_guide::toast_expired helper)
        check(!ui_guide::toast_expired(t, 103.9, 5.0), "toast alive 4.9 s after its stamp");
        check(ui_guide::toast_expired(t, 104.1, 5.0), "toast expired 5.1 s after its stamp");
        const unsigned gBefore = store.generation();
        store.push_unlock(0, "Nov 10, 2026", 100.0);
        check(store.generation() != gBefore, "push_unlock bumps the store generation");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
