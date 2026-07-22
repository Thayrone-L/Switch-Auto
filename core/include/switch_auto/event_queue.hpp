#pragma once

#include <functional>
#include <mutex>
#include <queue>

namespace switch_auto {

class EventQueue {
public:
    using Event = std::function<void()>;

    void post(Event event) {
        if (!event) return;
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push(std::move(event));
    }

    std::size_t drain(std::size_t maximum = 64) {
        std::size_t processed = 0;
        while (processed < maximum) {
            Event event;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (events_.empty()) break;
                event = std::move(events_.front());
                events_.pop();
            }
            event();
            ++processed;
        }
        return processed;
    }

private:
    std::mutex mutex_;
    std::queue<Event> events_;
};

}  // namespace switch_auto
