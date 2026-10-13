#pragma once
#include <string>
#include <vector>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/widgets/ListView.hpp"

namespace fenrir {

// Directory browsing engine: state (current directory, entries, error) + navigation
// across directories. This is NOT a Widget — the model is composed by DirectoryView
// (and through it by FileDialog / the file manager) so that everyone shares
// exactly the same listing logic instead of rewriting it.
//
// Selection/scrolling lives in ListView (widget) — this model is pure.
class DirectoryListing {
public:
    explicit DirectoryListing(std::string startDir);

    const std::string& currentDir() const { return dir_; }
    const std::vector<fs::Entry>& entries() const { return entries_; }
    bool hasError() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    bool empty() const { return entries_.empty(); }

    void reload(); // re-read the current directory (e.g. after file operations)
    void goUp();   // enter the parent directory

    const fs::Entry* at(int idx) const;         // nullptr if no entry
    std::string pathAt(int idx) const;          // full path of the entry (".." -> parent)
    bool isFileAt(int idx) const;               // true = regular file
    bool isParentAt(int idx) const;             // true = the ".." entry
    // Enter the directory at the given index.
    // true = entered (directory reloaded); false = it's a file — caller decides.
    bool enterAt(int idx);

    // ListView items for the current directory — the ONLY place file names are
    // converted to on-screen text (UTF-8 -> UTF-32 via text::toUtf32, "/ " prefix
    // for directories — identical appearance to before, everywhere).
    std::vector<ListView::Item> items() const;

private:
    std::string dir_;
    std::vector<fs::Entry> entries_;
    std::string error_;

    void loadDir(const std::string& dir);
};

} // namespace fenrir
