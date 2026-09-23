#include "agra/core/event_bus.hpp"

#include <algorithm>

namespace agra::core {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

void EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [type_idx, list] : subscribers_) {
        auto it = std::remove_if(list.begin(), list.end(), [id](const Subscription& sub) {
            return sub.id == id;
        });
        if (it != list.end()) {
            list.erase(it, list.end());
            return;
        }
    }
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
}

} // namespace agra::core
