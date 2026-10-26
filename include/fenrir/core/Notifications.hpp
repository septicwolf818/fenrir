#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace fenrir::notifications {

// A single notification emitted by an application or by Fenrir itself.
// Notifications are kept in the bus until cleared; unread ones are "missed".
struct Notification {
    int id = 0;
    std::string source;   // app id/name that produced it
    std::string title;
    std::string body;
    std::chrono::system_clock::time_point when{};
    bool read = false;
};

// Global notification bus. Apps push through AppContext::notify (or directly
// through notifications::global().push). The bus persists unread entries so
// missed notifications survive a restart.
class Bus {
public:
    Bus() = default;

    // Push a notification. Returns its id, or -1 when notifications are disabled.
    int push(std::string source, std::string title, std::string body = {});
    int push(const Notification& n);

    const std::vector<Notification>& items() const { return items_; }
    std::vector<Notification> unread() const;
    std::vector<Notification> missed() const { return unread(); }
    int unreadCount() const;
    bool empty() const { return items_.empty(); }

    void markRead(int id);
    void markAllRead();
    void remove(int id);
    void clear();

    bool enabled() const { return enabled_; }
    void setEnabled(bool v) { enabled_ = v; }

    void load();
    void save() const;
    void markDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_; }
    void saveIfDirty() const { if (dirty_) save(); }

private:
    std::vector<Notification> items_;
    int nextId_ = 1;
    bool enabled_ = true;
    mutable bool dirty_ = false;
};

using NotificationBus = Bus;

Bus& global();

// Convenience free functions operating on the global bus.
int notify(const std::string& source, const std::string& title, const std::string& body = {});

inline int push(const std::string& source, const std::string& title, const std::string& body = {}) {
    return global().push(source, title, body);
}

} // namespace fenrir::notifications