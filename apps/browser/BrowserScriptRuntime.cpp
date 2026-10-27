#include "BrowserScriptRuntime.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "BrowserNetwork.hpp"
#include "litehtml/el_script.h"
#include "quickjs.h"

namespace fenrir::apps::browser {
namespace {

constexpr std::size_t kMaxScriptBytes = 1024 * 1024;
constexpr std::size_t kRuntimeMemoryLimit = 24 * 1024 * 1024;
constexpr int kMaxModules = 16;
constexpr int kMaxPendingJobs = 128;
constexpr auto kScriptBudget = std::chrono::milliseconds(500);

struct RuntimeHost {
    std::string pageUrl;
    std::chrono::steady_clock::time_point deadline;
    int moduleRequests = 0;
};

int interruptRuntime(JSRuntime*, void* opaque) {
    const auto& host = *static_cast<RuntimeHost*>(opaque);
    return std::chrono::steady_clock::now() >= host.deadline;
}

std::string exceptionText(JSContext* context) {
    JSValue exception = JS_GetException(context);
    const char* message = JS_ToCString(context, exception);
    std::string result = message ? message : "JavaScript exception";
    if (message) JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    return result;
}

char* normalizeModule(JSContext* context, const char* baseName, const char* moduleName,
                      void* opaque) {
    auto& host = *static_cast<RuntimeHost*>(opaque);
    const std::string base = baseName && *baseName ? baseName : host.pageUrl;
    const std::string resolved = browser::resolveUrl(base, moduleName ? moduleName : "");
    if (resolved.empty()) {
        (void)JS_ThrowReferenceError(context, "Blocked non-HTTP JavaScript module URL");
        return nullptr;
    }
    return js_strdup(context, resolved.c_str());
}

JSModuleDef* loadModule(JSContext* context, const char* moduleName, void* opaque) {
    auto& host = *static_cast<RuntimeHost*>(opaque);
    if (++host.moduleRequests > kMaxModules) {
        (void)JS_ThrowRangeError(context, "Page imports too many JavaScript modules");
        return nullptr;
    }
    const std::string name = moduleName ? moduleName : "";
    Response response = browser::fetch(name, kMaxScriptBytes, 1500);
    if (!response.error.empty()) {
        (void)JS_ThrowReferenceError(context, "Cannot load module %s: %s",
                                     name.c_str(), response.error.c_str());
        return nullptr;
    }
    host.deadline = std::chrono::steady_clock::now() + kScriptBudget;
    JSValue compiled = JS_Eval(context, response.body.data(), response.body.size(), name.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) return nullptr;
    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JS_FreeValue(context, compiled);
    return module;
}

bool runJobs(JSRuntime* runtime, JSContext* context, std::string& error) {
    for (int count = 0; count < kMaxPendingJobs; ++count) {
        JSContext* jobContext = nullptr;
        const int result = JS_ExecutePendingJob(runtime, &jobContext);
        if (result == 0) return true;
        if (result < 0) {
            error = exceptionText(jobContext ? jobContext : context);
            return false;
        }
    }
    error = "JavaScript produced too many pending jobs";
    return false;
}

const char* kDomShim = R"JS(
(() => {
  const nodes = Object.create(null);
  const escapeHtml = value => String(value).replace(/&/g, '&amp;')
    .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  class FenrirNode {
    constructor(tag, id = '', fragment = false) {
      this.tagName = String(tag || 'div').toUpperCase();
      this.id = id;
      this._html = '';
      this._text = '';
      this._children = [];
      this._attributes = Object.create(null);
      this._classes = new Set();
      this._dirty = false;
      this._fragment = fragment;
      this.style = Object.create(null);
      Object.defineProperty(this, 'className', {
        get: () => Array.from(this._classes).join(' '),
        set: value => { this._classes = new Set(String(value).split(/\s+/).filter(Boolean)); }
      });
      this.classList = {
        add: (...names) => names.forEach(name => this._classes.add(String(name))),
        remove: (...names) => names.forEach(name => this._classes.delete(String(name))),
        contains: name => this._classes.has(String(name)),
        toggle: (name, force) => {
          name = String(name);
          const on = force === undefined ? !this._classes.has(name) : !!force;
          if (on) this._classes.add(name); else this._classes.delete(name);
          return on;
        }
      };
    }
    get innerHTML() { return this._html + this._children.map(node => node.outerHTML).join(''); }
    set innerHTML(value) { this._html = String(value); this._children = []; this._dirty = true; }
    get textContent() { return this._text; }
    set textContent(value) {
      this._text = String(value);
      this._html = escapeHtml(this._text);
      this._children = [];
      this._dirty = true;
    }
    get outerHTML() {
      if (this._fragment) return this.innerHTML;
      const attrs = [];
      if (this.id) attrs.push('id="' + String(this.id).replace(/"/g, '&quot;') + '"');
      if (this.className) attrs.push('class="' + this.className.replace(/"/g, '&quot;') + '"');
      for (const [key, value] of Object.entries(this._attributes))
        if (key !== 'id' && key !== 'class') attrs.push(key + '="' + String(value).replace(/"/g, '&quot;') + '"');
      const tag = this.tagName.toLowerCase();
      return '<' + tag + (attrs.length ? ' ' + attrs.join(' ') : '') + '>' +
        this.innerHTML + '</' + tag + '>';
    }
    appendChild(child) {
      if (child && child._fragment) this._children.push(...child._children);
      else if (child) this._children.push(child);
      this._dirty = true;
      return child;
    }
    setAttribute(name, value) {
      name = String(name); value = String(value);
      if (name === 'id') this.id = value;
      else if (name === 'class') this.className = value;
      else this._attributes[name] = value;
    }
    getAttribute(name) {
      if (name === 'id') return this.id || null;
      if (name === 'class') return this.className || null;
      return Object.prototype.hasOwnProperty.call(this._attributes, name) ? this._attributes[name] : null;
    }
    removeAttribute(name) { delete this._attributes[String(name)]; }
    addEventListener() {}
    querySelectorAll() { return []; }
    getBoundingClientRect() { return {top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0}; }
    focus() {}
  }
  const document = {
    documentElement: new FenrirNode('html'),
    getElementById(id) {
      id = String(id);
      return nodes[id] || (nodes[id] = new FenrirNode('div', id));
    },
    createElement(tag) { return new FenrirNode(tag); },
    createTextNode(text) { const node = new FenrirNode('#text'); node.textContent = text; return node; },
    createDocumentFragment() { return new FenrirNode('#fragment', '', true); },
    querySelectorAll() { return []; },
    querySelector() { return null; },
    addEventListener() {}
  };
  globalThis.document = document;
  globalThis.window = {
    innerHeight: 768, scrollY: 0, addEventListener() {}, scrollTo() {},
    matchMedia: () => ({matches: true, addEventListener() {}})
  };
  globalThis.location = {hash: '', href: ''};
  globalThis.requestAnimationFrame = () => 0;
  globalThis.IntersectionObserver = class { constructor() {} observe() {} unobserve() {} disconnect() {} };
  globalThis.console = {log() {}, info() {}, warn() {}, error() {}};
  globalThis.__fenrirNodes = nodes;
  globalThis.__fenrirTakePatches = () => Object.values(nodes)
    .filter(node => node._dirty && node.id)
    .map(node => ({id: node.id, html: node.innerHTML}));
})();
)JS";

std::string getString(JSContext* context, JSValueConst value) {
    const char* text = JS_ToCString(context, value);
    if (!text) return {};
    std::string result(text);
    JS_FreeCString(context, text);
    return result;
}

bool evaluate(JSContext* context, RuntimeHost& host, const std::string& source,
              const std::string& filename, bool module, std::string& error) {
    host.deadline = std::chrono::steady_clock::now() + kScriptBudget;
    JSValue result;
    if (module) {
        JSValue compiled = JS_Eval(context, source.data(), source.size(), filename.c_str(),
                                   JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) {
            error = exceptionText(context);
            return false;
        }
        result = JS_EvalFunction(context, compiled);
    } else {
        result = JS_Eval(context, source.data(), source.size(), filename.c_str(),
                         JS_EVAL_TYPE_GLOBAL);
    }
    if (JS_IsException(result)) {
        error = exceptionText(context);
        return false;
    }
    JS_FreeValue(context, result);
    return runJobs(JS_GetRuntime(context), context, error);
}

} // namespace

bool BrowserScriptRuntime::execute(const litehtml::document::ptr& document,
                                   const std::string& pageUrl,
                                   std::vector<DomPatch>& patches,
                                   std::string& error) const {
    patches.clear();
    error.clear();
    if (!document || !document->root()) return true;

    JSRuntime* runtime = JS_NewRuntime();
    if (!runtime) { error = "Could not create JavaScript runtime."; return false; }
    JS_SetMemoryLimit(runtime, kRuntimeMemoryLimit);
    JS_SetMaxStackSize(runtime, 256 * 1024);
    RuntimeHost host{pageUrl, std::chrono::steady_clock::now() + kScriptBudget};
    JS_SetInterruptHandler(runtime, interruptRuntime, &host);
    JS_SetModuleLoaderFunc(runtime, normalizeModule, loadModule, &host);
    JSContext* context = JS_NewContext(runtime);
    if (!context) {
        JS_FreeRuntime(runtime);
        error = "Could not create JavaScript context.";
        return false;
    }

    bool success = evaluate(context, host, kDomShim, "fenrir:dom", false, error);
    std::vector<litehtml::element::ptr> scripts;
    std::function<void(const litehtml::element::ptr&)> collect = [&](const litehtml::element::ptr& element) {
        if (std::string(element->get_tagName()) == "script") scripts.push_back(element);
        for (const auto& child : element->children()) collect(child);
    };
    collect(document->root());

    int inlineIndex = 0;
    for (const auto& script : scripts) {
        const char* typeAttr = script->get_attr("type");
        const std::string type = typeAttr ? typeAttr : "";
        if (type == "application/ld+json") continue;
        const bool module = type == "module";
        const char* srcAttr = script->get_attr("src");
        std::string filename = pageUrl + "#inline-" + std::to_string(inlineIndex++);
        std::string source;
        if (srcAttr && *srcAttr) {
            filename = browser::resolveUrl(pageUrl, srcAttr);
            if (filename.empty()) continue;
            Response response = browser::fetch(filename, kMaxScriptBytes, 2000);
            if (!response.error.empty()) continue;
            if (!response.finalUrl.empty()) filename = response.finalUrl;
            source = std::move(response.body);
        } else {
            const auto* scriptNode = dynamic_cast<const litehtml::el_script*>(script.get());
            if (scriptNode) source = scriptNode->script_text();
        }
        if (source.empty() || source.size() > kMaxScriptBytes) continue;
        std::string scriptError;
        if (!evaluate(context, host, source, filename, module, scriptError)) {
            // Continue with later scripts; one unsupported API should not
            // suppress independent page content.
            success = false;
            if (error.empty()) error = std::move(scriptError);
        }
    }

    JSValue global = JS_GetGlobalObject(context);
    JSValue nodes = JS_GetPropertyStr(context, global, "__fenrirNodes");
    JSPropertyEnum* properties = nullptr;
    uint32_t propertyCount = 0;
    if (JS_IsObject(nodes) &&
        JS_GetOwnPropertyNames(context, &properties, &propertyCount, nodes,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t i = 0; i < propertyCount; ++i) {
            const char* idValue = JS_AtomToCString(context, properties[i].atom);
            if (!idValue) continue;
            const std::string id(idValue);
            JS_FreeCString(context, idValue);
            JSValue node = JS_GetProperty(context, nodes, properties[i].atom);
            JSValue dirty = JS_GetPropertyStr(context, node, "_dirty");
            if (JS_ToBool(context, dirty) > 0) {
                JSValue html = JS_GetPropertyStr(context, node, "innerHTML");
                patches.push_back(DomPatch{id, getString(context, html)});
                JS_FreeValue(context, html);
            }
            JS_FreeValue(context, dirty);
            JS_FreeValue(context, node);
        }
        JS_FreePropertyEnum(context, properties, propertyCount);
    }
    JS_FreeValue(context, nodes);
    JS_FreeValue(context, global);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return success;
}

} // namespace fenrir::apps::browser
