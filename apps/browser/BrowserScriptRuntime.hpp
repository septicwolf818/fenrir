#pragma once

#include <memory>
#include <string>
#include <vector>

#include "litehtml.h"

struct JSRuntime;
struct JSContext;

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
                 std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error) const;
};

// A long-lived QuickJS context for a single page. Keeps the runtime (and its
// event handlers) alive so user interactions can dispatch into JavaScript.
class BrowserScriptContext {
public:
    BrowserScriptContext();
    ~BrowserScriptContext();

    BrowserScriptContext(const BrowserScriptContext&) = delete;
    BrowserScriptContext& operator=(const BrowserScriptContext&) = delete;

    bool initialize(const litehtml::document::ptr& document, const std::string& pageUrl,
                    std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error);

    // Dispatch a named event at the node identified by targetId (empty => document).
    // Returns newly produced patches.
    bool dispatchEvent(const std::string& targetId, const std::string& type,
                       std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error);

    bool evaluateSnippet(const std::string& code, std::string& error);
    std::string evalString(const std::string& code, std::string& error);

    void recordError(const std::string& msg) { jsErrors_.push_back(msg); }
    const std::vector<std::string>& jsErrors() const { return jsErrors_; }
    void clearErrors() { jsErrors_.clear(); }

    bool valid() const { return context_ != nullptr; }
    litehtml::document::ptr document() const { return document_; }

private:
    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    void* host_ = nullptr; // RuntimeHost*
    std::string pageUrl_;
    litehtml::document::ptr document_;
    std::vector<std::string> jsErrors_;
    bool setupRuntime(const std::string& pageUrl, std::string& error);
    void teardown();
    bool collectPatches(std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error);
    bool runScripts(const litehtml::document::ptr& document, std::string& error);
};

} // namespace fenrir::apps::browser