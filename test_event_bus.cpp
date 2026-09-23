#include "test_framework.hpp"
#include "agra/core/event_bus.hpp"

using namespace agra::core;

AGRA_TEST_CASE("EventBus - Subscription and Notification") {
    auto& bus = EventBus::instance();
    bus.clear();

    int call_count = 0;
    ProcessId last_pid = 0;

    auto sub_id = bus.subscribe<GameDetectedEvent>([&](const GameDetectedEvent& ev) {
        call_count++;
        last_pid = ev.pid;
    });

    GameDetectedEvent ev1;
    ev1.pid = 1234;
    ev1.process_name = "Cyberpunk2077.exe";
    bus.publish(ev1);

    AGRA_CHECK_EQ(call_count, 1);
    AGRA_CHECK_EQ(last_pid, 1234u);

    // Unsubscribe and verify no more events received
    bus.unsubscribe(sub_id);

    GameDetectedEvent ev2;
    ev2.pid = 5678;
    ev2.process_name = "Valorant.exe";
    bus.publish(ev2);

    AGRA_CHECK_EQ(call_count, 1);
    AGRA_CHECK_EQ(last_pid, 1234u);
}

AGRA_TEST_CASE("EventBus - Independent Multiple Subscribers") {
    auto& bus = EventBus::instance();
    bus.clear();

    int count_a = 0;
    int count_b = 0;

    bus.subscribe<BottleneckDetectedEvent>([&](const BottleneckDetectedEvent&) {
        count_a++;
    });

    bus.subscribe<BottleneckDetectedEvent>([&](const BottleneckDetectedEvent&) {
        count_b++;
    });

    BottleneckDetectedEvent ev;
    ev.bottleneck = BottleneckType::CpuBound;
    bus.publish(ev);

    AGRA_CHECK_EQ(count_a, 1);
    AGRA_CHECK_EQ(count_b, 1);
}
