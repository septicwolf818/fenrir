#pragma once
#include <map>
#include <string>
#include <string_view>

namespace fenrir::prefs {

// Application preferences — ONE key=value file per application:
//   ~/.config/fenrir/apps/<appId>.conf
// (the $HOME/.config/fenrir directory already exists thanks to settings.ini; the
// apps/ subdirectory is created by Prefs on first save). Format as in settings.ini:
// `key=value`, lines starting with '#'/';' and empty lines are ignored. Unknown keys
// are kept in the map on load - the application may ignore them, so
// adding/removing keys is backward and forward compatible.
//
// Usage in an application:
//   auto& p = prefs::forApp("editor");   // cached per-process instance
//   bool spaces = p.getBool("insert_spaces", false);
//   p.setBool("insert_spaces", true);    // writes the file immediately
//
// set* writes the file immediately (small files - no dirty tracking, applications
// don't have to remember to call save()). save() is also public (bulk saves).
class Prefs {
public:
    // Loads the app file for the given appId (no file = empty map).
    void load(std::string_view appId);

    // Writes the whole map to ~/.config/fenrir/apps/<appId>.conf
    // (creating directories). Returns false when the file cannot be written.
    bool save() const;

    std::string get(const std::string& key, const std::string& fallback = {}) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    int getInt(const std::string& key, int fallback = 0) const;

    // Sets and saves the file. set(key, "") removes the key (keeps the file tidy).
    void set(const std::string& key, const std::string& value);
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int value);

    std::string_view appId() const { return appId_; }
    // Full file path (for messages/debugging).
    std::string path() const;

private:
    std::string appId_;
    std::map<std::string, std::string> kv_;
};

// Preferences instance for a given application, cached for the whole process -
// all windows of the same application share one map (save = one file).
// appId should be stable and in English ("editor", "calendar", "paint").
Prefs& forApp(std::string_view appId);

} // namespace fenrir::prefs
