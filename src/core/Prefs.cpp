#include "fenrir/core/Prefs.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "fenrir/core/FileSystem.hpp"

namespace fenrir::prefs {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

bool parseBool(std::string_view v, bool fallback) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return fallback;
}

} // namespace

std::string Prefs::path() const {
    return fs::join(fs::join(fs::join(fs::join(fs::homeDir(), ".config"), "fenrir"),
                              "apps"),
                    appId_ + ".conf");
}

void Prefs::load(std::string_view appId) {
    appId_ = std::string(appId);
    kv_.clear();
    // Bezpieczna nazwa pliku: appId bez '/' (aplikacje podaja stale slugi,
    // ale obrona przed sciezka z zewnatrz nie zaszkodzi).
    for (char& c : appId_)
        if (c == '/' || c == '\\') c = '_';
    const std::string file = path(); // lokalna kopia — strumien nie trzyma temporara
    std::ifstream f(file);
    if (!f) return; // brak pliku = pusta mapa (aplikacja uzywa defaultow)

    std::string line;
    while (std::getline(f, line)) {
        std::string sv = trim(line);
        if (sv.empty() || sv.front() == '#' || sv.front() == ';') continue;
        std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key = trim(sv.substr(0, eq));
        if (key.empty()) continue;
        kv_[key] = trim(sv.substr(eq + 1));
    }
}

bool Prefs::save() const {
    std::error_code ec;
    std::filesystem::path p(path());
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::trunc);
    if (!f) return false;
    f << "# Preferencje aplikacji Fenrir (" << appId_ << "; key=value)\n";
    for (const auto& [k, v] : kv_) f << k << '=' << v << '\n';
    return true;
}

std::string Prefs::get(const std::string& key, const std::string& fallback) const {
    auto it = kv_.find(key);
    return it == kv_.end() ? fallback : it->second;
}

bool Prefs::getBool(const std::string& key, bool fallback) const {
    auto it = kv_.find(key);
    return it == kv_.end() ? fallback : parseBool(it->second, fallback);
}

int Prefs::getInt(const std::string& key, int fallback) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(it->second.c_str(), &end, 10);
    return (end && *end == '\0') ? static_cast<int>(v) : fallback;
}

void Prefs::set(const std::string& key, const std::string& value) {
    if (value.empty()) kv_.erase(key);
    else kv_[key] = value;
    save();
}

void Prefs::setBool(const std::string& key, bool value) {
    set(key, value ? "1" : "0");
}

void Prefs::setInt(const std::string& key, int value) {
    set(key, std::to_string(value));
}

Prefs& forApp(std::string_view appId) {
    static std::unordered_map<std::string, std::unique_ptr<Prefs>> cache;
    auto& slot = cache[std::string(appId)];
    if (!slot) {
        slot = std::make_unique<Prefs>();
        slot->load(appId);
    }
    return *slot;
}

} // namespace fenrir::prefs
