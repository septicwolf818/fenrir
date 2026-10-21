#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fenrir {

// FileWatchdog reports changes made to watched paths OUTSIDE the process.
//
// A watched path may be a file or a directory:
//   - a file changes when its content changes (modification time or size),
//   - a directory changes when entries are added, removed or renamed (its
//     modification time),
//   - a path that no longer exists is reported as removed.
//
// It is a plain model — no widget and no timer: the owner decides when to
// poll (usually from draw(), which runs once per frame). Polls are cheap:
// one filesystem query per watched path, and the interval check makes a poll
// that happens too early a no-op.
//
// The file tree (expanded directories), the file manager (the listed
// directory) and the text editor (the opened file) all use this class, so the
// "did something change on disk?" logic exists only once.
class FileWatchdog {
public:
    // Snapshot of a path on disk.
    struct State {
        bool exists = false;
        bool isDir = false;
        std::filesystem::file_time_type mtime{};
        std::uintmax_t size = 0;
    };

    enum class Change { Created, Modified, Removed };

    struct Event {
        std::string path;
        Change change = Change::Modified;
    };

    // Minimum time between two polls; poll() returns no events before the
    // interval has passed.
    static constexpr int kPollIntervalMs = 500;

    // Current state of a path (exists / is a directory / time / size).
    static State stateOf(const std::string& path);

    // True when the current state differs from a remembered one: the file
    // disappeared, or it was modified (new modification time or new size).
    static bool differsFrom(const State& remembered, const State& current);

    // Starts watching a path. The first poll records its state and does NOT
    // report it as a change.
    void watch(const std::string& path);
    void unwatch(const std::string& path);
    void clear();
    bool watches(const std::string& path) const;
    std::size_t watchedCount() const { return states_.size(); }

    // Polls the watched paths (respecting the interval) and returns what
    // changed since the previous poll.
    std::vector<Event> poll();
    // Polls right away, ignoring the interval (used by explicit refreshes).
    std::vector<Event> pollNow();

private:
    // Remembered state plus a "seen" flag: the first observation of a path is
    // only recorded, never reported as a change.
    struct Entry {
        State state;
        bool seen = false;
    };

    std::unordered_map<std::string, Entry> states_;
    std::chrono::steady_clock::time_point lastPoll_{};
};

} // namespace fenrir
