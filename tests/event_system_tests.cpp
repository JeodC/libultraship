#include <gtest/gtest.h>
#include <atomic>
#include <stdexcept>
#include <functional>
#include <thread>
#include <vector>
#include "ship/events/EventSystem.h"

// ============================================================
// Thunk mechanism
//
// EventCallback is a plain C function pointer (void(*)(IEvent*)).
// Capturing lambdas cannot be implicitly converted to function pointers,
// so we provide a small set of static thunk slots that delegate to an
// std::function set by each test.  Tests reset their slots when done.
// ============================================================

namespace {
static std::function<void(IEvent*)> gSlot[4];
static void Slot0(IEvent* e) {
    if (gSlot[0])
        gSlot[0](e);
}
static void Slot1(IEvent* e) {
    if (gSlot[1])
        gSlot[1](e);
}
static void Slot2(IEvent* e) {
    if (gSlot[2])
        gSlot[2](e);
}
static void Slot3(IEvent* e) {
    if (gSlot[3])
        gSlot[3](e);
}
static void Noop(IEvent*) {
}
} // namespace

// ============================================================
// RegisterEvent
// ============================================================

TEST(EventSystem, RegisterEventReturnsValidId) {
    Ship::EventSystem sys;
    EventID id = sys.RegisterEvent("TestEvent");
    EXPECT_GE(id, 0);
}

TEST(EventSystem, RegisterMultipleEventsReturnDistinctIds) {
    Ship::EventSystem sys;
    EventID a = sys.RegisterEvent("EventA");
    EventID b = sys.RegisterEvent("EventB");
    EXPECT_NE(a, b);
}

TEST(EventSystem, RegisterEventNullNameUsesDefault) {
    Ship::EventSystem sys;
    EventID id = sys.RegisterEvent(nullptr);
    auto reg = sys.GetEventRegistration(id);
    ASSERT_TRUE(reg.has_value());
    EXPECT_STREQ(reg->Name, "Unknown");
}

TEST(EventSystem, GetEventRegistrationReturnsNullForUnknownId) {
    Ship::EventSystem sys;
    EXPECT_FALSE(sys.GetEventRegistration(999).has_value());
}

// ============================================================
// RegisterListener
// ============================================================

TEST(EventSystem, RegisterListenerReturnsValidId) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    ListenerID lid = sys.RegisterListener(evId, Noop);
    EXPECT_GE(lid, 0);
}

TEST(EventSystem, RegisterListenerOnUnregisteredEventThrows) {
    Ship::EventSystem sys;
    EXPECT_THROW(sys.RegisterListener(-1, Noop), std::runtime_error);
}

TEST(EventSystem, MultipleListenersGetDistinctIds) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    ListenerID a = sys.RegisterListener(evId, Noop);
    ListenerID b = sys.RegisterListener(evId, Noop);
    EXPECT_NE(a, b);
}

// ============================================================
// UnregisterListener
// ============================================================

TEST(EventSystem, UnregisterListenerPreventsCallback) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    int callCount = 0;
    gSlot[0] = [&](IEvent*) { callCount++; };
    ListenerID lid = sys.RegisterListener(evId, Slot0);

    sys.UnregisterListener(evId, lid);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    EXPECT_EQ(callCount, 0);
    gSlot[0] = nullptr;
}

TEST(EventSystem, UnregisterNonExistentListenerIsNoOp) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    EXPECT_NO_THROW(sys.UnregisterListener(evId, 9999));
}

TEST(EventSystem, UnregisterWithInvalidEventIdIsNoOp) {
    Ship::EventSystem sys;
    EXPECT_NO_THROW(sys.UnregisterListener(-1, 0));
}

TEST(EventSystem, UnregisterWithInvalidListenerIdIsNoOp) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    EXPECT_NO_THROW(sys.UnregisterListener(evId, -1));
}

// ============================================================
// CallEvent
// ============================================================

TEST(EventSystem, CallEventInvokesRegisteredListener) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    bool called = false;
    gSlot[0] = [&](IEvent*) { called = true; };
    sys.RegisterListener(evId, Slot0);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    EXPECT_TRUE(called);
    gSlot[0] = nullptr;
}

TEST(EventSystem, CallEventPassesEventPointer) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    IEvent* received = nullptr;
    gSlot[0] = [&](IEvent* e) { received = e; };
    sys.RegisterListener(evId, Slot0);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    EXPECT_EQ(received, &ev);
    gSlot[0] = nullptr;
}

TEST(EventSystem, CallEventInvokesAllListeners) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    int count = 0;
    gSlot[0] = [&](IEvent*) { count++; };
    gSlot[1] = [&](IEvent*) { count++; };
    gSlot[2] = [&](IEvent*) { count++; };
    sys.RegisterListener(evId, Slot0);
    sys.RegisterListener(evId, Slot1);
    sys.RegisterListener(evId, Slot2);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    EXPECT_EQ(count, 3);
    gSlot[0] = gSlot[1] = gSlot[2] = nullptr;
}

TEST(EventSystem, CallEventUpdatesCallerMetadata) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    sys.RegisterListener(evId, Noop);

    const char* key = "file.cpp:42";
    IEvent ev{ false };
    sys.CallEvent(evId, &ev, "file.cpp", 42, key);

    auto reg = sys.GetEventRegistration(evId);
    ASSERT_TRUE(reg.has_value());
    auto it = reg->Callers.find(key);
    ASSERT_NE(it, reg->Callers.end());
    EXPECT_EQ(it->second.Count, 1u);
    EXPECT_EQ(it->second.Line, 42);
}

TEST(EventSystem, CallEventIncrementsCaller) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    const char* key = "key";
    IEvent ev{ false };
    sys.CallEvent(evId, &ev, "f.cpp", 1, key);
    sys.CallEvent(evId, &ev, "f.cpp", 1, key);
    sys.CallEvent(evId, &ev, "f.cpp", 1, key);

    auto reg = sys.GetEventRegistration(evId);
    ASSERT_TRUE(reg.has_value());
    EXPECT_EQ(reg->Callers.at(key).Count, 3u);
}

// ============================================================
// Listener priority ordering
// ============================================================

TEST(EventSystem, ListenersSortedByPriority) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    std::vector<int> order;
    // Register in arbitrary order; listeners should be sorted by priority
    // (EventPriority enum: LOW=0, NORMAL=1, HIGH=2)
    gSlot[0] = [&](IEvent*) { order.push_back(0); }; // LOW
    gSlot[1] = [&](IEvent*) { order.push_back(2); }; // HIGH
    gSlot[2] = [&](IEvent*) { order.push_back(1); }; // NORMAL

    sys.RegisterListener(evId, Slot0, EVENT_PRIORITY_LOW);
    sys.RegisterListener(evId, Slot1, EVENT_PRIORITY_HIGH);
    sys.RegisterListener(evId, Slot2, EVENT_PRIORITY_NORMAL);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);

    // std::lower_bound inserts at the first position where priority >= new priority,
    // so the vector is sorted ascending: [LOW, NORMAL, HIGH]
    ASSERT_EQ(order.size(), 3u);
    EXPECT_LT(order[0], order[2]); // LOW before HIGH
    EXPECT_EQ(order[1], 1);        // NORMAL in the middle

    gSlot[0] = gSlot[1] = gSlot[2] = nullptr;
}

// ============================================================
// GetEventRegistrations
// ============================================================

TEST(EventSystem, GetEventRegistrationsReflectsAllRegistered) {
    Ship::EventSystem sys;
    sys.RegisterEvent("A");
    sys.RegisterEvent("B");
    sys.RegisterEvent("C");
    EXPECT_EQ(sys.GetEventRegistrations().size(), 3u);
}

TEST(EventSystem, GetEventRegistrationsIsACopy) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    auto before = sys.GetEventRegistrations();
    sys.RegisterListener(evId, Noop);
    EXPECT_EQ(before.at(evId).Listeners.size(), 0u);
    EXPECT_EQ(sys.GetEventRegistrations().at(evId).Listeners.size(), 1u);
}

// ============================================================
// Changes during a dispatch
// ============================================================

TEST(EventSystem, ListenerAddedDuringDispatchRunsFromTheNextOne) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    int added = 0;
    gSlot[1] = [&](IEvent*) { added++; };
    gSlot[0] = [&](IEvent*) {
        if (added == 0) {
            sys.RegisterListener(evId, Slot1);
        }
    };
    sys.RegisterListener(evId, Slot0);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    EXPECT_EQ(added, 0);
    sys.CallEvent(evId, &ev);
    EXPECT_EQ(added, 1);
    gSlot[0] = gSlot[1] = nullptr;
}

TEST(EventSystem, ListenerRemovedDuringDispatchIsGoneFromTheNextOne) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");

    int removedCalls = 0;
    ListenerID removed = -1;
    gSlot[0] = [&](IEvent*) { sys.UnregisterListener(evId, removed); };
    gSlot[1] = [&](IEvent*) { removedCalls++; };
    sys.RegisterListener(evId, Slot0, EVENT_PRIORITY_LOW);
    removed = sys.RegisterListener(evId, Slot1, EVENT_PRIORITY_HIGH);

    IEvent ev{ false };
    sys.CallEvent(evId, &ev);
    const int afterFirst = removedCalls;
    sys.CallEvent(evId, &ev);
    EXPECT_LE(afterFirst, 1);
    EXPECT_EQ(removedCalls, afterFirst);
    gSlot[0] = gSlot[1] = nullptr;
}

// ============================================================
// Threads
// ============================================================

namespace {
static std::atomic<int> gStableCalls{ 0 };
static void Stable(IEvent*) {
    gStableCalls++;
}
} // namespace

TEST(EventSystem, DispatchWhileAnotherThreadRegisters) {
    Ship::EventSystem sys;
    EventID evId = sys.RegisterEvent("Ev");
    gStableCalls = 0;
    sys.RegisterListener(evId, Stable);

    constexpr int kDispatches = 20000;
    std::thread game([&] {
        IEvent ev{ false };
        for (int i = 0; i < kDispatches; i++) {
            sys.CallEvent(evId, &ev);
            if (i % 100 == 0) {
                sys.RegisterEvent("PerActor");
            }
        }
    });
    for (int i = 0; i < kDispatches; i++) {
        ListenerID id = sys.RegisterListener(evId, Noop, EVENT_PRIORITY_HIGH);
        sys.UnregisterListener(evId, id);
        if (i % 1000 == 0) {
            sys.GetEventRegistrations();
        }
    }
    game.join();
    EXPECT_EQ(gStableCalls.load(), kDispatches);
}
