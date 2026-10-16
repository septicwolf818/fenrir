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
    if (ec) throw std::runtime_error("Cannot read directory: " + dir + " (" + ec.message() + ")");

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
    if (par.empty()) return dir; // already at the root (or a relative path without '/')
    return par.string();
}

std::string absolute(const std::string& path) {
    std::error_code ec;
    stdfs::path p = stdfs::absolute(path, ec);
    if (ec) return path;
    std::string result = p.lexically_normal().string();
    // Strip trailing '/' unless the path is exactly "/"
    if (result.size() > 1 && result.back() == '/')
        result.pop_back();
    return result;
}

std::string join(const std::string& dir, const std::string& name) {
    stdfs::path p = stdfs::path(dir) / name;
    return p.lexically_normal().string();
}

std::string nameOf(const std::string& path) {
    return stdfs::path(path).filename().string();
}

void copyFile(const std::string& from, const std::string& to) {
    // CRITICAL: std::ofstream(to, trunc) opens/truncates THE TARGET FILE
    // immediately, before we read anything from the source. If from==to
    // (the same path), it TRUNCATES THE SOURCE before reading - real
    // data loss, caught in tests. We check this BEFORE opening
    // anything.
    std::error_code ec;
    if (stdfs::exists(from, ec) && stdfs::exists(to, ec) &&
        stdfs::equivalent(from, to, ec) && !ec) {
        throw std::runtime_error("Source and destination are the same file: " + from);
    }

    std::ifstream in(from, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open source: " + from);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write destination: " + to);
    out << in.rdbuf();
    if (!out) throw std::runtime_error("Write error: " + to);
}

long long fileSize(const std::string& path) {
    std::error_code ec;
    auto sz = stdfs::file_size(path, ec);
    if (ec) return -1;
    return static_cast<long long>(sz);
}

void removeFile(const std::string& path) {
    std::error_code ec;
    bool removed = stdfs::remove(path, ec);
    if (!removed) throw std::runtime_error("Cannot remove file: " + path + " (" + ec.message() + ")");
}

void copyPath(const std::string& from, const std::string& to) {
    std::error_code ec;
    if (isDirectory(from)) {
        // stdfs::copy(recursive) does not guard against copying a directory into
        // itself or into its own descendant - it recurses until the disk fills up.
        // Prefix with a trailing '/': "/ab" is not inside "/a".
        const std::string prefix = from == "/" ? "/" : from + "/";
        if (to == from || (!to.empty() && to.compare(0, prefix.size(), prefix) == 0))
            throw std::runtime_error("Cannot copy a directory into itself: " + from);
        stdfs::copy(from, to, stdfs::copy_options::recursive |
                                   stdfs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("Cannot copy directory: " + from + " (" + ec.message() + ")");
    } else {
        copyFile(from, to);
    }
}

void renamePath(const std::string& from, const std::string& to) {
    std::error_code ec;
    stdfs::rename(from, to, ec);
    if (ec) throw std::runtime_error("Cannot rename/move: " + from + " (" + ec.message() + ")");
}

void removePath(const std::string& path) {
    std::error_code ec;
    auto removed = stdfs::remove_all(path, ec);
    if (ec || removed == 0)
        throw std::runtime_error("Cannot remove: " + path + " (" + ec.message() + ")");
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    return "/";
}

} // namespace fenrir::fs
