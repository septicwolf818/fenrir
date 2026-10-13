#include "fenrir/app/FileAssociationRegistry.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "fenrir/app/AppRegistry.hpp"
#include "fenrir/core/FileSystem.hpp"

namespace fenrir {

namespace {

std::string trim(std::string_view value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string normalizeExtension(std::string extension) {
    extension = trim(extension);
    if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

} // namespace

std::string FileAssociationRegistry::path() {
    return fs::join(fs::join(fs::join(fs::homeDir(), ".config"), "fenrir"),
                    "file-associations.ini");
}

std::string FileAssociationRegistry::extensionForPath(const std::string& filePath) {
    return normalizeExtension(std::filesystem::path(filePath).extension().string());
}

void FileAssociationRegistry::load(const AppRegistry& apps) {
    associations_.clear();
    for (const auto& app : apps.apps())
        for (const auto& extension : app->fileExtensions())
            associations_[normalizeExtension(extension)] = app->id();

    std::ifstream input(path());
    if (!input) {
        save(); // Persist the built-in defaults on first use.
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string entry = trim(line);
        if (entry.empty() || entry.front() == '#' || entry.front() == ';') continue;
        const std::size_t equals = entry.find('=');
        if (equals == std::string::npos) continue;

        const std::string extension = normalizeExtension(entry.substr(0, equals));
        const std::string appId = trim(std::string_view(entry).substr(equals + 1));
        const App* app = apps.findById(appId);
        if (!extension.empty() && app && !app->fileExtensions().empty())
            associations_[extension] = appId;
    }
}

bool FileAssociationRegistry::associateFile(const std::string& filePath,
                                             const std::string& appId,
                                             const AppRegistry& apps) {
    const std::string extension = extensionForPath(filePath);
    const App* app = apps.findById(appId);
    if (extension.empty() || !app || app->fileExtensions().empty()) return false;
    associations_[extension] = appId;
    return save();
}

std::string FileAssociationRegistry::appIdForPath(const std::string& filePath) const {
    const std::string extension = extensionForPath(filePath);
    const auto it = associations_.find(extension);
    return it == associations_.end() ? std::string() : it->second;
}

bool FileAssociationRegistry::save() const {
    const std::filesystem::path file(path());
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) return false;

    std::ofstream output(file, std::ios::trunc);
    if (!output) return false;
    output << "# Fenrir file associations (extension=app-id)\n";
    for (const auto& [extension, appId] : associations_)
        output << extension << '=' << appId << '\n';
    output.flush();
    if (!output) return false;
    output.close();
    return !output.fail();
}

} // namespace fenrir
