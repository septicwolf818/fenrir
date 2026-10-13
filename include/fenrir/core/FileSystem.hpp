#pragma once
#include <string>
#include <vector>

namespace fenrir::fs {

struct Entry {
    std::string name;
    bool isDir = false;
};

// Lists directory contents: directories first (alphabetically), then
// files (alphabetically). Does NOT include the synthetic ".." - that is
// DirectoryListing's job (see widgets/DirectoryListing.hpp), since only it
// knows whether showing an "up one level" entry makes sense in a given context.
// Throws std::runtime_error on failure (no access, directory does not exist).
std::vector<Entry> list(const std::string& dir);

bool isDirectory(const std::string& path);
bool exists(const std::string& path);

// Parent directory; for "/" returns "/" (there is nothing above).
std::string parent(const std::string& dir);

// Absolute path (relative to cwd, normalized). Panel starting
// directories MUST be absolute - for relative paths (".", "foo")
// parent() returns itself and "up one level" navigation collapses.
std::string absolute(const std::string& path);

// Joins a directory and an entry name into a proper path (no double '/').
std::string join(const std::string& dir, const std::string& name);

// Last path element (file/directory name). "" for paths ending in '/'.
std::string nameOf(const std::string& path);

// Copies file contents byte for byte. Throws std::runtime_error on failure.
void copyFile(const std::string& from, const std::string& to);

// Copies a FILE or a DIRECTORY (recursively). Throws std::runtime_error.
void copyPath(const std::string& from, const std::string& to);

// Moves/renames a file or directory. Throws std::runtime_error.
void renamePath(const std::string& from, const std::string& to);

// Removes a file or directory (recursively). Throws std::runtime_error.
void removePath(const std::string& path);

// File size in bytes, or -1 if it cannot be read.
long long fileSize(const std::string& path);

// Removes a single file. Does NOT handle directories. Throws on error.
void removeFile(const std::string& path);

// $HOME from the environment (works both as 'pi' on Raspberry Pi OS and as root
// in the test container); "/" as the last-resort fallback.
std::string homeDir();

} // namespace fenrir::fs
