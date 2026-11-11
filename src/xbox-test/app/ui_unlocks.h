// SPDX-License-Identifier: GPL-3.0-or-later
// Pure glue between steam_achievements.h (schema + gbe's saved earned set) and
// the in-game guide (ui_guide.h): achievement views, earned-set diffs for the
// unlock poller and the CDN icon URLs. No Win32, no app globals, so
// tests/ui-unlocks.cpp exercises all of it natively.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "steam_achievements.h"
#include "ui_guide.h"

namespace ui_unlocks {

// One schema achievement + the player's earned state -> a guide view row.
// Hidden and locked rows hide their real name/description, as the mockup does.
inline ui_guide::AchievementView to_view(const steam_achievements::Achievement& a,
                                         const steam_achievements::Earned& e) {
    ui_guide::AchievementView v;
    v.unlocked = e.earned;
    v.hidden = a.hidden;
    if (a.hidden && !e.earned) {
        v.name = "Hidden achievement";
        v.description = "Details are hidden until unlocked";
    } else {
        v.name = a.display.empty() ? a.name : a.display;
        v.description = a.description;
    }
    if (e.earned && e.time) v.earned = ui_guide::format_date(static_cast<long long>(e.time));
    return v;
}

// The whole guide list, in schema order (the store's index == this position).
inline std::vector<ui_guide::AchievementView>
build_views(const steam_achievements::Schema& s, const std::map<std::string, steam_achievements::Earned>& earned) {
    std::vector<ui_guide::AchievementView> out;
    out.reserve(s.achievements.size());
    for (const auto& a : s.achievements) {
        auto it = earned.find(a.name);
        out.push_back(to_view(a, it != earned.end() ? it->second : steam_achievements::Earned{}));
    }
    return out;
}

// Names of achievements earned in `now` but not in `before` (the poller's diff).
inline std::vector<std::string> newly_earned(const std::map<std::string, steam_achievements::Earned>& before,
                                             const std::map<std::string, steam_achievements::Earned>& now) {
    std::vector<std::string> out;
    for (const auto& [name, e] : now)
        if (e.earned) {
            auto it = before.find(name);
            if (it == before.end() || !it->second.earned) out.push_back(name);
        }
    return out;
}

// Index of a schema achievement (by its internal name) in the guide list
// (the toast/push_unlock mapping). Schema order == view order, so the search
// is linear and short.
inline int index_of(const steam_achievements::Schema& s, const std::vector<ui_guide::AchievementView>& all,
                    const std::string& name) {
    for (size_t i = 0; i < s.achievements.size() && i < all.size(); ++i)
        if (s.achievements[i].name == name) return int(i);
    return -1;
}

// Steam community CDN URL for an achievement icon hash (public images, no
// account data). Empty hash -> empty URL (the view keeps its fallback tile).
// The schema's icon/icon_gray values already carry their extension
// ("4c378f...ef4c.jpg"), so the suffix is added only when it is missing: the
// doubled ".jpg.jpg" the old code produced 404'd every icon on the console.
inline std::string icon_url(uint32_t appid, const std::string& hash) {
    if (hash.empty()) return {};
    const std::string base = "https://cdn.fastly.steamstatic.com/steamcommunity/public/images/apps/" +
                             std::to_string(appid) + "/" + hash;
    return hash.find('.') == std::string::npos ? base + ".jpg" : base;
}

}  // namespace ui_unlocks
