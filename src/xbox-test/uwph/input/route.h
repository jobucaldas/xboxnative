// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <optional>
#include <vector>

namespace wxl::input {
enum class Focus { launcher, guest, none };
enum class Edge { press, release };
struct Event { std::string button; Edge edge; };
struct Sink { std::vector<Event> events; void send(const Event& e) { events.push_back(e); } };

class Router {
public:
    Router(Sink& launcher, Sink& guest) : launcher_(launcher), guest_(guest) {}
    void focus(Focus f) {
        if (f != focus_ && pressed_) {
            sink_for_press_->send(Event{*pressed_, Edge::release});
            pressed_.reset();
            sink_for_press_ = nullptr;
        }
        focus_ = f;
    }
    void press(const std::string& button) {
        if (focus_ == Focus::none || pressed_) return;
        sink_for_press_ = &(focus_ == Focus::guest ? guest_ : launcher_);
        sink_for_press_->send(Event{button, Edge::press});
        pressed_ = button;
    }
    void release() {
        if (!pressed_) return;
        sink_for_press_->send(Event{*pressed_, Edge::release});
        pressed_.reset();
        sink_for_press_ = nullptr;
    }
private:
    Sink& launcher_; Sink& guest_; Focus focus_{Focus::launcher};
    std::optional<std::string> pressed_;
    Sink* sink_for_press_{nullptr};
};
}
