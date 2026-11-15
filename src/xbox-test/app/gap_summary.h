// SPDX-License-Identifier: GPL-3.0-or-later
// Per-launch import outcomes collected at the binder boundary.
#pragma once
#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gap_summary {
inline thread_local bool collecting = false;
enum class Outcome { resolved, routed, donor, stub, missing };
inline constexpr size_t kMissingLimit = 64;
using Import = std::pair<std::string, std::string>;

struct Report {
    std::vector<std::string> lines;
};

class Binder {
    std::map<Import, Outcome> imports_;
public:
    // Rebinding a provisional missing import to a mapped game DLL replaces its
    // outcome without double-counting the import.
    void record(const std::string& dll, const std::string& symbol, Outcome outcome) {
        imports_[{dll, symbol}] = outcome;
    }
    void record_if_absent(const std::string& dll, const std::string& symbol, Outcome outcome) {
        imports_.emplace(Import{dll,symbol},outcome);
    }

    Report report(const std::string& exe, const char* arch) const {
        Report out;
        using Counts = std::array<size_t, 5>;
        std::map<std::string, Counts> by_dll;
        Counts total{};
        for (const auto& item : imports_) {
            auto index = static_cast<size_t>(item.second);
            ++by_dll[item.first.first][index];
            ++total[index];
        }
        auto dll_line = [](const std::string& dll, const Counts& c) {
            return "GAP: dll=" + dll + " resolved=" + std::to_string(c[0]) +
                " routed=" + std::to_string(c[1]) + " donor=" + std::to_string(c[2]) +
                " stub=" + std::to_string(c[3]) + " missing=" + std::to_string(c[4]);
        };
        for (const auto& item : by_dll) out.lines.push_back(dll_line(item.first, item.second));
        out.lines.push_back("GAP: summary exe=" + exe + " arch=" + arch +
            " resolved=" + std::to_string(total[0]) + " routed=" + std::to_string(total[1]) +
            " donor=" + std::to_string(total[2]) + " stub=" + std::to_string(total[3]) +
            " missing=" + std::to_string(total[4]));
        size_t emitted = 0;
        for (const auto& item : imports_) {
            if (item.second != Outcome::missing) continue;
            if (emitted == kMissingLimit) break;
            out.lines.push_back("GAP: missing " + item.first.first + "!" + item.first.second);
            ++emitted;
        }
        size_t missing = total[static_cast<size_t>(Outcome::missing)];
        if (missing > emitted)
            out.lines.push_back("GAP: missing truncated after " + std::to_string(kMissingLimit) +
                " names total=" + std::to_string(missing));
        return out;
    }
};
} // namespace gap_summary
