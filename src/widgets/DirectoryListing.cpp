#include "fenrir/widgets/DirectoryListing.hpp"

#include "fenrir/core/Text.hpp"

namespace fenrir {

DirectoryListing::DirectoryListing(std::string startDir) { loadDir(std::move(startDir)); }

void DirectoryListing::loadDir(const std::string& dir) {
    dir_ = dir;
    entries_.clear();
    error_.clear();

    std::string par = fs::parent(dir_);
    if (par != dir_) entries_.push_back(fs::Entry{"..", true});

    try {
        auto listed = fs::list(dir_);
        entries_.insert(entries_.end(), listed.begin(), listed.end());
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void DirectoryListing::reload() { loadDir(dir_); }

void DirectoryListing::goUp() { loadDir(fs::parent(dir_)); }

const fs::Entry* DirectoryListing::at(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(entries_.size())) return nullptr;
    return &entries_[static_cast<std::size_t>(idx)];
}

std::string DirectoryListing::pathAt(int idx) const {
    const fs::Entry* e = at(idx);
    if (!e) return dir_;
    if (e->name == "..") return fs::parent(dir_);
    return fs::join(dir_, e->name);
}

bool DirectoryListing::isFileAt(int idx) const {
    const fs::Entry* e = at(idx);
    return e && !e->isDir;
}

bool DirectoryListing::isParentAt(int idx) const {
    const fs::Entry* e = at(idx);
    return e && e->name == "..";
}

bool DirectoryListing::enterAt(int idx) {
    const fs::Entry* e = at(idx);
    if (!e || !e->isDir) return false;
    if (e->name == "..") loadDir(fs::parent(dir_));
    else loadDir(fs::join(dir_, e->name));
    return true;
}

std::vector<ListView::Item> DirectoryListing::items() const {
    std::vector<ListView::Item> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) {
        ListView::Item item;
        item.text = (e.isDir ? U"/ " : U"  ") + text::toUtf32(e.name);
        item.dim = (e.name == "..");
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace fenrir
