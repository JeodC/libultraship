#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "ship/events/EventTypes.h"

namespace Ship {

/**
 * @brief Tracks listener registration metadata for a single event.
 *
 * EventRegistration is a copy of the bookkeeping record stored for each registered
 * event ID. It holds the event's debug name, all active listeners sorted by priority,
 * and a map of callers that have invoked the event (used for diagnostics/profiling).
 */
struct EventRegistration {
    /** @brief Optional human-readable name for this event (used in debug output). */
    const char* Name;
    /** @brief Monotonically increasing counter used to assign unique ListenerIDs. */
    ListenerID NextListenerID = 0;
    /** @brief Map from caller location string to call-site metadata (file, line, count). */
    std::unordered_map<const char*, EventMetadata> Callers;
    /** @brief Active listeners for this event, ordered by priority (highest first). */
    std::vector<EventListener> Listeners;
};

/**
 * @brief Lightweight publish/subscribe event bus.
 *
 * EventSystem allows decoupled subsystems to communicate without direct dependencies.
 * Events are identified by numeric EventIDs (allocated by RegisterEvent()) and may
 * carry arbitrary payload structs derived from IEvent.
 *
 * Listeners are invoked synchronously in priority order whenever CallEvent() is called.
 * Any listener may set IEvent::Cancelled to true; CallEvent() does not enforce
 * cancellation itself — the caller is responsible for checking it via the
 * CALL_CANCELLABLE_EVENT macro.
 *
 * Every method is safe to call from any thread, including from inside a listener.
 * A dispatch runs the listeners registered when it started: one added meanwhile
 * runs from the next dispatch, and one removed meanwhile may still run once.
 *
 * Typical usage (C++ side):
 * @code
 * // Registration (once at startup)
 * REGISTER_EVENT(MyEvent);
 *
 * // Listening
 * REGISTER_LISTENER(MyEvent, EVENT_PRIORITY_NORMAL, [](IEvent* e) {
 *     auto* event = static_cast<MyEvent*>(e);
 *     // handle...
 * });
 *
 * // Dispatching
 * CALL_EVENT(MyEvent, payload);
 * @endcode
 */
class EventSystem {
  public:
    /**
     * @brief Allocates a new unique EventID and optionally assigns it a debug name.
     * @param name Optional human-readable name for diagnostics (may be nullptr).
     * @return Newly allocated EventID (always >= 0).
     */
    EventID RegisterEvent(const char* name = nullptr);

    /**
     * @brief Subscribes a callback to an event.
     *
     * The listener is inserted into the event's listener list sorted by @p priority
     * (higher priority listeners are called first). If multiple listeners share the
     * same priority they are called in registration order.
     *
     * @param id       EventID returned by RegisterEvent().
     * @param callback Function to invoke when the event fires.
     * @param priority Dispatch priority; defaults to EVENT_PRIORITY_NORMAL.
     * @param file     Source file of the registration site (for diagnostics).
     * @param line     Source line of the registration site (for diagnostics).
     * @return Unique ListenerID that can be passed to UnregisterListener().
     */
    ListenerID RegisterListener(EventID id, EventCallback callback, EventPriority priority = EVENT_PRIORITY_NORMAL,
                                const char* file = nullptr, int line = 0);

    /**
     * @brief Removes a previously registered listener.
     * @param id         EventID the listener was registered with.
     * @param listenerId ListenerID returned by RegisterListener().
     */
    void UnregisterListener(EventID id, ListenerID listenerId);

    /**
     * @brief Dispatches an event to all registered listeners in priority order.
     *
     * Each listener's callback is invoked with a pointer to @p event. Listeners may
     * set IEvent::Cancelled to signal that subsequent handling should be skipped
     * (the caller must check this via the CALL_CANCELLABLE_EVENT macro family).
     *
     * @param id    EventID of the event to fire.
     * @param event Pointer to the event payload struct.
     * @param file  Source file of the call site (for diagnostics).
     * @param line  Source line of the call site (for diagnostics).
     * @param key   Optional string key used to deduplicate diagnostic metadata.
     */
    void CallEvent(EventID id, IEvent* event, const char* file = nullptr, int line = 0, const char* key = nullptr);

    /**
     * @brief Returns a copy of the EventRegistration for the given ID, or std::nullopt if not found.
     * @param id EventID to look up.
     */
    std::optional<EventRegistration> GetEventRegistration(EventID id);

    /**
     * @brief Returns a copy of the complete event registry map (EventID → EventRegistration).
     *
     * Intended for inspection/tooling; prefer the typed accessors for normal use.
     */
    std::unordered_map<EventID, EventRegistration> GetEventRegistrations();

  private:
    // Listeners is replaced whole on every change, never edited in place, so a dispatch can run the list it took
    // without holding the lock.
    struct Entry {
        const char* Name;
        ListenerID NextListenerID = 0;
        std::unordered_map<const char*, EventMetadata> Callers;
        std::shared_ptr<const std::vector<EventListener>> Listeners = std::make_shared<std::vector<EventListener>>();
    };

    static EventRegistration Copy(const Entry& entry);

    std::shared_mutex mMutex;
    std::unordered_map<EventID, Entry> mEventRegistry;
    EventID mInternalEventID = 0;
};

} // namespace Ship