#include "ship/events/EventSystem.h"
#include <stdexcept>
#include <algorithm>
#include <mutex>

#include "ship/events/CoreEvents.h"

namespace Ship {

EventID EventSystem::RegisterEvent(const char* name) {
    std::unique_lock lock(this->mMutex);
    const EventID id = this->mInternalEventID++;
    this->mEventRegistry[id] = Entry{ .Name = name == nullptr ? "Unknown" : name };
    return id;
}

ListenerID EventSystem::RegisterListener(EventID id, EventCallback callback, EventPriority priority, const char* file,
                                         int line) {
    if (id == -1) {
        throw std::runtime_error("Trying to register listener for unregistered event");
    }

    std::unique_lock lock(this->mMutex);
    auto& registry = this->mEventRegistry[id];
    const EventListener newListener = { registry.NextListenerID++, priority, callback, { file, line, 0 } };

    auto listeners = std::make_shared<std::vector<EventListener>>(*registry.Listeners);
    auto insertIt = std::lower_bound(listeners->begin(), listeners->end(), newListener,
                                     [](const EventListener& existingListener, const EventListener& listenerToInsert) {
                                         return existingListener.Priority < listenerToInsert.Priority;
                                     });

    listeners->insert(insertIt, newListener);
    registry.Listeners = std::move(listeners);

    return newListener.ID;
}

void EventSystem::UnregisterListener(EventID id, ListenerID listenerId) {
    if (id == -1) {
        return;
    }

    if (listenerId == -1) {
        return;
    }

    std::unique_lock lock(this->mMutex);
    auto& registry = this->mEventRegistry[id];

    auto it = std::find_if(registry.Listeners->begin(), registry.Listeners->end(),
                           [listenerId](const EventListener& listener) { return listener.ID == listenerId; });

    if (it == registry.Listeners->end()) {
        return;
    }

    auto listeners = std::make_shared<std::vector<EventListener>>(*registry.Listeners);
    listeners->erase(listeners->begin() + (it - registry.Listeners->begin()));
    registry.Listeners = std::move(listeners);
}

void EventSystem::CallEvent(const EventID id, IEvent* event, const char* file, const int line, const char* key) {
    std::shared_ptr<const std::vector<EventListener>> listeners;
    {
        std::shared_lock lock(this->mMutex);
        auto it = this->mEventRegistry.find(id);
        if (it != this->mEventRegistry.end()) {
            listeners = it->second.Listeners;
        }
    }

    if (listeners != nullptr) {
        for (const auto& listener : *listeners) {
            listener.Function(event);
        }
    }

#ifdef _DEBUG
    // Event Debugger is a dev tool and shouldn't be compiled into Releases
    std::unique_lock lock(this->mMutex);
    auto& info = this->mEventRegistry[id].Callers[key];

    if (info.Path == nullptr) {
        info.Path = file;
        info.Line = line;
    }

    info.Count++;
#endif
}

EventRegistration EventSystem::Copy(const Entry& entry) {
    return EventRegistration{ .Name = entry.Name,
                              .NextListenerID = entry.NextListenerID,
                              .Callers = entry.Callers,
                              .Listeners = *entry.Listeners };
}

std::optional<EventRegistration> EventSystem::GetEventRegistration(EventID id) {
    std::shared_lock lock(this->mMutex);
    auto it = this->mEventRegistry.find(id);
    if (it == this->mEventRegistry.end()) {
        return std::nullopt;
    }
    return Copy(it->second);
}

std::unordered_map<EventID, EventRegistration> EventSystem::GetEventRegistrations() {
    std::shared_lock lock(this->mMutex);
    std::unordered_map<EventID, EventRegistration> registrations;
    for (const auto& [id, entry] : this->mEventRegistry) {
        registrations.emplace(id, Copy(entry));
    }
    return registrations;
}

} // namespace Ship
