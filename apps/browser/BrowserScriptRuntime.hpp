#pragma once

#include <string>
#include <vector>

#include "litehtml.h"

namespace fenrir::apps::browser {

class BrowserScriptRuntime {
public:
    struct DomPatch {
        std::string id;
        std::string html;
    };

    // Execute a page's scripts in a bounded QuickJS context with the small DOM
    // surface used by static pages. DOM mutations are returned as HTML patches
    // for LiteHTML to lay out; OS and filesystem APIs are not installed.
    bool execute(const litehtml::document::ptr& document, const std::string& pageUrl,
                 std::vector<DomPatch>& patches, std::string& error) const;
};

} // namespace fenrir::apps::browser
