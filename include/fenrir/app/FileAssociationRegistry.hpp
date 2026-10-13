#pragma once

#include <map>
#include <string>

namespace fenrir {

class AppRegistry;

// Global, persistent mapping from lowercase file extensions to app IDs.
class FileAssociationRegistry {
public:
    void load(const AppRegistry& apps);
    bool associateFile(const std::string& path, const std::string& appId,
                       const AppRegistry& apps);
    std::string appIdForPath(const std::string& path) const;

    static std::string extensionForPath(const std::string& path);
    static std::string path();

private:
    bool save() const;
    std::map<std::string, std::string> associations_;
};

} // namespace fenrir
