#pragma once

#include "agra/core/types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <typeindex>
#include <unordered_map>

namespace agra::core {

// Base class for all AGRA events
struct Event {
    virtual ~Event() = default;
    TimePoint timestamp{std::chrono::steady_clock::now()};
};

// Concrete Event Definitions
struct GameDetectedEvent : public Event {
    ProcessId pid{0};
    std::string process_name;
    std::string executable_path;
};

struct GameExitedEvent : public Event {
    ProcessId pid{0};
    std::string process_name;
};

struct WorkloadSampleEvent : public Event {
    ProcessId pid{0};
    WorkloadMetrics metrics;
};

struct BottleneckDetectedEvent : public Event {
    ProcessId pid{0};
    BottleneckType bottleneck{BottleneckType::Unknown};
    double confidence{0.0};
    std::string explanation;
};

struct AllocationDecisionEvent : public Event {
    AllocationDecision decision;
};

struct PolicyChangedEvent : public Event {
    ProcessId pid{0};
    PriorityLevel old_priority{PriorityLevel::Normal};
    PriorityLevel new_priority{PriorityLevel::Normal};
    AffinityMask old_affinity{0};
    AffinityMask new_affinity{0};
    std::string reason;
};

struct RollbackTriggeredEvent : public Event {
    ProcessId pid{0};
    std::string reason;
    bool success{false};
};

struct SafetyViolationEvent : public Event {
    ProcessId pid{0};
    std::string description;
};

struct BenchmarkProgressEvent : public Event {
    std::string phase_name;
    double percent_complete{0.0};
    double metric_value{0.0};
    std::string details;
};

// Generic Type-Safe Event Bus
class EventBus {
public:
    using SubscriptionId = std::uint64_t;

    static EventBus& instance();

    template <typename EventType>
    SubscriptionId subscribe(std::function<void(const EventType&)> handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto type_idx = std::type_index(typeid(EventType));
        SubscriptionId id = next_sub_id_++;

        auto wrapper = [handler](const Event& ev) {
            handler(static_cast<const EventType&>(ev));
        };

        subscribers_[type_idx].push_back({id, std::move(wrapper)});
        return id;
    }

    template <typename EventType>
    void publish(const EventType& event) {
        std::vector<std::function<void(const Event&)>> callbacks_to_invoke;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscribers_.find(std::type_index(typeid(EventType)));
            if (it != subscribers_.end()) {
                for (const auto& sub : it->second) {
                    callbacks_to_invoke.push_back(sub.callback);
                }
            }
        }

        // Invoke outside of the lock to prevent re-entrancy deadlocks
        for (const auto& cb : callbacks_to_invoke) {
            cb(event);
        }
    }

    void unsubscribe(SubscriptionId id);
    void clear();

private:
    EventBus() = default;
    ~EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    struct Subscription {
        SubscriptionId id;
        std::function<void(const Event&)> callback;
    };

    std::mutex mutex_;
    SubscriptionId next_sub_id_{1};
    std::unordered_map<std::type_index, std::vector<Subscription>> subscribers_;
};

} // namespace agra::core
