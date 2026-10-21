#include "fenrir/core/FileWatchdog.hpp"

namespace fenrir {

FileWatchdog::State FileWatchdog::stateOf(const std::string& path) {
    State state;
    std::error_code ec;
    // A missing path is reported as file_type::not_found; whether the error
    // code is also set varies between standard libraries, so the type is
    // checked first and the error code afterwards.
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (status.type() == std::filesystem::file_type::not_found) return state;
    if (ec) return state;

    state.exists = true;
    state.isDir = std::filesystem::is_directory(status);

    std::error_code timeEc;
    const auto mtime = std::filesystem::last_write_time(path, timeEc);
    if (!timeEc) state.mtime = mtime;

    if (!state.isDir) {
        std::error_code sizeEc;
        const auto size = std::filesystem::file_size(path, sizeEc);
        if (!sizeEc) state.size = size;
    }
    return state;
}

bool FileWatchdog::differsFrom(const State& remembered, const State& current) {
    if (remembered.exists != current.exists) return true;
    if (!current.exists) return false; // both missing — nothing changed
    if (remembered.mtime != current.mtime) return true;
    // The size catches writes that keep the modification time (some editors
    // and network filesystems do that); directories have no meaningful size.
    if (!current.isDir && remembered.size != current.size) return true;
    return false;
}

void FileWatchdog::watch(const std::string& path) {
    if (path.empty()) return;
    auto [it, inserted] = states_.try_emplace(path);
    if (!inserted) return; // already watched — keep the remembered state
    it->second.seen = false; // the first poll only records the state
}

void FileWatchdog::unwatch(const std::string& path) { states_.erase(path); }

void FileWatchdog::clear() {
    states_.clear();
    lastPoll_ = std::chrono::steady_clock::time_point{};
}

bool FileWatchdog::watches(const std::string& path) const {
    return states_.find(path) != states_.end();
}

std::vector<FileWatchdog::Event> FileWatchdog::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (lastPoll_ != std::chrono::steady_clock::time_point{} &&
        now - lastPoll_ < std::chrono::milliseconds(kPollIntervalMs))
        return {};
    return pollNow();
}

std::vector<FileWatchdog::Event> FileWatchdog::pollNow() {
    lastPoll_ = std::chrono::steady_clock::now();

    std::vector<Event> events;
    for (auto& [path, entry] : states_) {
        const State current = stateOf(path);
        if (!entry.seen) {
            // First observation of this path — record it, but do not report
            // the current state as a change.
            entry.state = current;
            entry.seen = true;
            continue;
        }
        if (entry.state.exists && !current.exists) {
            events.push_back(Event{path, Change::Removed});
        } else if (!entry.state.exists && current.exists) {
            events.push_back(Event{path, Change::Created});
        } else if (differsFrom(entry.state, current)) {
            events.push_back(Event{path, Change::Modified});
        }
        entry.state = current;
    }
    return events;
}

} // namespace fenrir
