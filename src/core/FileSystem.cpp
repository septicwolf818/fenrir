#include "fenrir/core/FileSystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace stdfs = std::filesystem;

namespace fenrir::fs {

std::vector<Entry> list(const std::string& dir) {
    std::vector<Entry> dirs, files;
    std::error_code ec;
    stdfs::directory_iterator it(dir, stdfs::directory_options::skip_permission_denied, ec);
    if (ec) throw std::runtime_error("Nie mozna odczytac katalogu: " + dir + " (" + ec.message() + ")");

    for (const auto& entry : it) {
        std::error_code ec2;
        bool isDir = entry.is_directory(ec2);
        Entry e{entry.path().filename().string(), isDir && !ec2};
        if (e.isDir) dirs.push_back(std::move(e));
        else files.push_back(std::move(e));
    }
    auto byName = [](const Entry& a, const Entry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    dirs.insert(dirs.end(), files.begin(), files.end());
    return dirs;
}

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return stdfs::is_directory(path, ec) && !ec;
}

bool exists(const std::string& path) {
    std::error_code ec;
    return stdfs::exists(path, ec) && !ec;
}

std::string parent(const std::string& dir) {
    stdfs::path p(dir);
    stdfs::path par = p.parent_path();
    if (par.empty()) return dir; // juz jestesmy u korzenia (albo sciezka wzgledna bez '/')
    return par.string();
}

std::string join(const std::string& dir, const std::string& name) {
    stdfs::path p = stdfs::path(dir) / name;
    return p.lexically_normal().string();
}

void copyFile(const std::string& from, const std::string& to) {
    // KRYTYCZNE: std::ofstream(to, trunc) otwiera/czysci PLIK DOCELOWY
    // natychmiast, zanim cokolwiek przeczytamy ze zrodla. Jesli from==to
    // (ta sama sciezka), to TRUNCATES THE SOURCE przed odczytem - realna
    // utrata danych, zlapana w testach. Sprawdzamy to PRZED otwarciem
    // czegokolwiek.
    std::error_code ec;
    if (stdfs::exists(from, ec) && stdfs::exists(to, ec) &&
        stdfs::equivalent(from, to, ec) && !ec) {
        throw std::runtime_error("Zrodlo i cel to ten sam plik: " + from);
    }

    std::ifstream in(from, std::ios::binary);
    if (!in) throw std::runtime_error("Nie mozna otworzyc zrodla: " + from);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Nie mozna zapisac celu: " + to);
    out << in.rdbuf();
    if (!out) throw std::runtime_error("Blad zapisu: " + to);
}

long long fileSize(const std::string& path) {
    std::error_code ec;
    auto sz = stdfs::file_size(path, ec);
    if (ec) return -1;
    return static_cast<long long>(sz);
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    return "/";
}

} // namespace fenrir::fs
