#include "fenrir/core/Notifications.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>

#include "fenrir/core/Prefs.hpp"

namespace fenrir::notifications {

namespace {

std::vector<int> parseIds(const std::string& s) {
    std::vector<int> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(std::stoi(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::stoi(cur));
    return out;
}

std::string joinIds(const std::vector<Notification>& items) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& n : items) {
        if (!first) oss << ',';
        first = false;
        oss << n.id;
    }
    return oss.str();
}

} // namespace

int Bus::push(std::string source, std::string title, std::string body) {
    if (!enabled_) return -1;
    for (const auto& existing : items_) {
        if (existing.source == source && existing.title == title && existing.body == body)
            return existing.id;
    }
    Notification n;
    n.id = nextId_++;
    n.source = std::move(source);
    n.title = std::move(title);
    n.body = std::move(body);
    n.when = std::chrono::system_clock::now();
    n.read = false;
    items_.push_back(std::move(n));
    markDirty();
    return items_.back().id;
}

int Bus::push(const Notification& n) {
    if (!enabled_) return -1;
    for (const auto& existing : items_) {
        if (existing.source == n.source && existing.title == n.title && existing.body == n.body)
            return existing.id;
    }
    Notification copy = n;
    if (copy.id <= 0) copy.id = nextId_++;
    else nextId_ = std::max(nextId_, copy.id + 1);
    if (copy.when == std::chrono::system_clock::time_point{}) copy.when = std::chrono::system_clock::now();
    items_.push_back(std::move(copy));
    markDirty();
    return items_.back().id;
}

std::vector<Notification> Bus::unread() const {
    std::vector<Notification> out;
    for (const auto& n : items_)
        if (!n.read) out.push_back(n);
    return out;
}

int Bus::unreadCount() const {
    int c = 0;
    for (const auto& n : items_) if (!n.read) ++c;
    return c;
}

void Bus::markRead(int id) {
    for (auto& n : items_) if (n.id == id) n.read = true;
    markDirty();
}

void Bus::markAllRead() {
    for (auto& n : items_) n.read = true;
    markDirty();
}

void Bus::remove(int id) {
    std::erase_if(items_, [id](const Notification& n) { return n.id == id; });
    markDirty();
}

void Bus::clear() {
    items_.clear();
    markDirty();
}

void Bus::load() {
    auto& p = prefs::forApp("notifications");
    nextId_ = p.getInt("next_id", 1);
    items_.clear();
    const std::string ids = p.get("ids");
    for (int id : parseIds(ids)) {
        Notification n;
        n.id = id;
        n.source = p.get("n" + std::to_string(id) + "_source");
        n.title = p.get("n" + std::to_string(id) + "_title");
        n.body = p.get("n" + std::to_string(id) + "_body");
        const int when = p.getInt("n" + std::to_string(id) + "_when", 0);
        n.when = std::chrono::system_clock::time_point(std::chrono::seconds(when));
        n.read = p.getBool("n" + std::to_string(id) + "_read", false);
        if (n.title.empty() && n.source.empty()) continue;
        items_.push_back(std::move(n));
    }
    nextId_ = std::max(nextId_, 1);
    for (const auto& n : items_) nextId_ = std::max(nextId_, n.id + 1);
    dirty_ = false;
}

void Bus::save() const {
    if (!dirty_) return;
    dirty_ = false;
    auto& p = prefs::forApp("notifications");
    p.setInt("next_id", nextId_);
    p.set("ids", joinIds(items_));
    for (const auto& n : items_) {
        const std::string prefix = "n" + std::to_string(n.id) + "_";
        p.set(prefix + "source", n.source);
        p.set(prefix + "title", n.title);
        p.set(prefix + "body", n.body);
        p.setInt(prefix + "when", static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(n.when.time_since_epoch()).count()));
        p.setBool(prefix + "read", n.read);
    }
}

Bus& global() {
    static Bus instance;
    static bool loaded = false;
    if (!loaded) {
        instance.load();
        loaded = true;
    }
    return instance;
}

int notify(const std::string& source, const std::string& title, const std::string& body) {
    return global().push(source, title, body);
}

} // namespace fenrir::notifications