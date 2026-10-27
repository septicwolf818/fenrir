#include "BrowserScriptRuntime.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <atomic>
#include <cstdio>

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
    RuntimeHost(std::string url) : pageUrl(std::move(url)), deadline(std::chrono::steady_clock::now()) {}
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
    // Append stack if available.
    JSValue stack = JS_GetPropertyStr(context, exception, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
        const char* st = JS_ToCString(context, stack);
        if (st) { result += "\n"; result += st; JS_FreeCString(context, st); }
    }
    JS_FreeValue(context, stack);
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

JSValue jsCookieGet(JSContext* context, JSValueConst, int argc, JSValueConst*) {
    (void)argc;
    JSValue global = JS_GetGlobalObject(context);
    JSValue pu = JS_GetPropertyStr(context, global, "__pageUrl");
    const char* url = JS_ToCString(context, pu);
    std::string u = url ? url : "";
    if (url) JS_FreeCString(context, url);
    JS_FreeValue(context, pu);
    JS_FreeValue(context, global);
    std::string cookies = browser::getCookieString(u);
    return JS_NewString(context, cookies.c_str());
}

JSValue makeElementHandle(JSContext* context, BrowserScriptContext* ctx,
                          const litehtml::element::ptr& element) {
    if (!element) return JS_NULL;
    const std::string handle = ctx->registerElement(element);
    JSValue obj = JS_NewObject(context);
    const char* id = element->get_attr("id");
    JS_SetPropertyStr(context, obj, "id", JS_NewString(context, id ? id : ""));
    JS_SetPropertyStr(context, obj, "__handle", JS_NewString(context, handle.c_str()));
    const char* tagName = element->get_tagName();
    JS_SetPropertyStr(context, obj, "tag", JS_NewString(context, tagName ? tagName : ""));
    if (auto* tag = dynamic_cast<litehtml::html_tag*>(element.get())) {
        for (const auto& [name, value] : tag->get_attrs()) {
            JS_SetPropertyStr(context, obj, name.c_str(), JS_NewString(context, value.c_str()));
        }
    }
    return obj;
}

litehtml::element::ptr queryRoot(BrowserScriptContext* ctx, const std::string& selector) {
    if (!ctx || !ctx->document() || !ctx->document()->root()) return {};
    return ctx->document()->root()->select_one(selector);
}

litehtml::element::ptr queryScoped(BrowserScriptContext* ctx, const std::string& handle,
                                   const std::string& selector) {
    if (!ctx || !ctx->document() || !ctx->document()->root()) return {};
    auto root = ctx->resolveElement(handle);
    if (!root) return {};
    for (const auto& element : root->select_all(selector)) {
        if (element != root) return element;
    }
    return {};
}

JSValue jsQuerySelector(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    auto* ctx = static_cast<BrowserScriptContext*>(JS_GetContextOpaque(context));
    if (!ctx) return JS_NULL;
    const char* selectorValue = JS_ToCString(context, argv[0]);
    if (!selectorValue) return JS_EXCEPTION;
    const std::string selector(selectorValue);
    JS_FreeCString(context, selectorValue);
    std::string scope;
    if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        const char* scopeValue = JS_ToCString(context, argv[1]);
        if (scopeValue) { scope = scopeValue; JS_FreeCString(context, scopeValue); }
    }
    auto element = scope.empty() ? queryRoot(ctx, selector) : queryScoped(ctx, scope, selector);
    return element ? makeElementHandle(context, ctx, element) : JS_NULL;
}

JSValue jsQuerySelectorAll(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    JSValue array = JS_NewArray(context);
    if (argc < 1) return array;
    auto* ctx = static_cast<BrowserScriptContext*>(JS_GetContextOpaque(context));
    if (!ctx || !ctx->document() || !ctx->document()->root()) return array;
    const char* selectorValue = JS_ToCString(context, argv[0]);
    if (!selectorValue) return JS_EXCEPTION;
    const std::string selector(selectorValue);
    JS_FreeCString(context, selectorValue);
    std::string scope;
    if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        const char* scopeValue = JS_ToCString(context, argv[1]);
        if (scopeValue) { scope = scopeValue; JS_FreeCString(context, scopeValue); }
    }
    auto root = scope.empty() ? ctx->document()->root() : ctx->resolveElement(scope);
    if (!root) return array;
    uint32_t index = 0;
    for (const auto& element : root->select_all(selector)) {
        if (!scope.empty() && element == root) continue;
        JS_SetPropertyUint32(context, array, index++, makeElementHandle(context, ctx, element));
    }
    return array;
}

JSValue jsGetElementById(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    auto* ctx = static_cast<BrowserScriptContext*>(JS_GetContextOpaque(context));
    if (!ctx) return JS_NULL;
    const char* idValue = JS_ToCString(context, argv[0]);
    if (!idValue) return JS_EXCEPTION;
    const std::string id(idValue);
    JS_FreeCString(context, idValue);
    const auto element = ctx->findElementById(id);
    return element ? makeElementHandle(context, ctx, element) : JS_NULL;
}

JSValue jsCookieSet(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char* cookie = JS_ToCString(context, argv[0]);
    if (!cookie) return JS_EXCEPTION;
    std::string c(cookie);
    JS_FreeCString(context, cookie);
    JSValue global = JS_GetGlobalObject(context);
    JSValue pu = JS_GetPropertyStr(context, global, "__pageUrl");
    const char* url = JS_ToCString(context, pu);
    std::string u = url ? url : "";
    if (url) JS_FreeCString(context, url);
    JS_FreeValue(context, pu);
    JS_FreeValue(context, global);
    browser::setCookieString(u, c);
    return JS_UNDEFINED;
}

JSValue jsFetchSync(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(context, "fetch requires a URL");
    const char* url = JS_ToCString(context, argv[0]);
    if (!url) return JS_EXCEPTION;
    std::string u(url);
    JS_FreeCString(context, url);
    Response response = browser::fetch(u, kMaxScriptBytes, 5000);
    JSValue obj = JS_NewObject(context);
    const bool ok = response.error.empty() && response.status >= 200 && response.status < 300;
    JS_SetPropertyStr(context, obj, "ok", JS_NewBool(context, ok));
    JS_SetPropertyStr(context, obj, "status", JS_NewInt32(context, static_cast<int>(response.status)));
    JSValue body = JS_NewStringLen(context, response.body.data(), response.body.size());
    JS_SetPropertyStr(context, obj, "body", body);
    JS_SetPropertyStr(context, obj, "error", JS_NewString(context, response.error.c_str()));
    return obj;
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
  const __taskQueue = [];
  function __makeStorage() {
    const data = Object.create(null);
    return {
      getItem(k) { k = String(k); return Object.prototype.hasOwnProperty.call(data, k) ? data[k] : null; },
      setItem(k, v) { data[String(k)] = String(v); },
      removeItem(k) { delete data[String(k)]; },
      clear() { for (const k in data) delete data[k]; },
      key(i) { const ks = Object.keys(data); return i < ks.length ? ks[i] : null; },
      get length() { return Object.keys(data).length; }
    };
  }
  const __allNodes = [];
  const escapeHtml = value => String(value).replace(/&/g, '&amp;')
    .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  class FenrirNode {
    constructor(tag, id = '', fragment = false) {
        this.tagName = String(tag || 'div').toUpperCase();
        this.nodeType = fragment ? 11 : (this.tagName === '#TEXT' ? 3 : 1);
        this.id = id || '';
        this._html = '';
        this._text = '';
        this._children = [];
        this._attributes = Object.create(null);
        this._classes = new Set();
        this._dirty = false;
        this._fragment = fragment;
        this.parentNode = null;
        this.parentElement = null;
        if (!fragment) __allNodes.push(this);
        this.style = Object.create(null);
      // dataset: proxy for data-* attributes
      Object.defineProperty(this, 'dataset', {
        get() {
          const self = this;
          return new Proxy({}, {
            get(target, prop) {
              if (typeof prop === 'symbol') return target[prop];
              const attr = 'data-' + String(prop).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
              const v = self.getAttribute(attr);
              return v === null ? undefined : v;
            },
            set(target, prop, value) {
              const attr = 'data-' + String(prop).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
              self.setAttribute(attr, String(value));
              return true;
            }
          });
        },
        configurable: true, enumerable: false
      });
      // Expose common attributes as properties (like real DOM).
      ['href','src','value','title','name','type','action','method','alt','placeholder'].forEach(a => {
        Object.defineProperty(this, a, {
          get() { const v = this.getAttribute(a); return v === null ? '' : v; },
          set(v) { this.setAttribute(a, v); },
          configurable: true, enumerable: true
        });
      });
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
    set innerHTML(value) {
      value = String(value);
      this._html = '';
      this._children = [];
      // Parse the HTML string into child nodes so DOM queries work.
      this._parseHtml(value);
      this._dirty = true;
    }
    _parseHtml(html) {
      // Minimal HTML parser: handles tags, attributes, and text.
      const tagRe = /<\/?([a-zA-Z][a-zA-Z0-9]*)\b([^>]*)>|([^<]+)/g;
      const stack = [this];
      let m;
      while ((m = tagRe.exec(html)) !== null) {
        if (m[3]) {
          // text node
          const text = m[3];
          if (text.trim()) {
            const tn = new FenrirNode('#text');
            tn.textContent = text;
            stack[stack.length-1].appendChild(tn);
          }
          continue;
        }
        const closing = html[m.index+1] === '/';
        const tagName = m[1];
        const attrStr = m[2] || '';
        if (closing) {
          // pop stack until matching tag
          for (let i = stack.length-1; i > 0; i--) {
            if (stack[i].tagName.toLowerCase() === tagName.toLowerCase()) {
              stack.length = i;
              break;
            }
          }
          continue;
        }
        // void elements
        const voids = new Set(['area','base','br','col','embed','hr','img','input','link','meta','param','source','track','wbr']);
        const node = new FenrirNode(tagName);
        // parse attributes
        const attrRe = /([a-zA-Z_:][a-zA-Z0-9_.:-]*)\s*(?:=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
        let am;
        while ((am = attrRe.exec(attrStr)) !== null) {
          const name = am[1];
          const val = am[2] !== undefined ? am[2] : (am[3] !== undefined ? am[3] : (am[4] !== undefined ? am[4] : ''));
          node.setAttribute(name, val);
        }
        stack[stack.length-1].appendChild(node);
        if (!voids.has(tagName.toLowerCase()) && !html.slice(m.index).startsWith('</')) {
          // check if self-closing
          if (!attrStr.trim().endsWith('/')) {
            stack.push(node);
          }
        }
      }
    }
    get textContent() {
      if (this.nodeType === 3) return this._text;
      return this._text + this._children.map(node => node.textContent).join('');
    }
    set textContent(value) {
      this._text = String(value);
      this._html = escapeHtml(this._text);
      this._children = [];
      this._dirty = true;
    }
    get outerHTML() {
      if (this._fragment) return this.innerHTML;
      if (this.nodeType === 3) return escapeHtml(this._text);
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
      if (child && child._fragment) {
        child._children.forEach(c => { if (c) { c.parentNode = this; c.parentElement = this; } });
        this._children.push(...child._children);
      } else if (child) {
        child.parentNode = this;
        child.parentElement = this;
        this._children.push(child);
      }
      this._dirty = true;
      return child;
    }
    get children() { return this._children; }
    get childNodes() { return this._children; }
    get firstChild() { return this._children.length ? this._children[0] : null; }
    get lastChild() { return this._children.length ? this._children[this._children.length-1] : null; }
    get nextSibling() {
      if (!this.parentNode) return null;
      const idx = this.parentNode._children.indexOf(this);
      return idx >= 0 && idx + 1 < this.parentNode._children.length ? this.parentNode._children[idx+1] : null;
    }
    get previousSibling() {
      if (!this.parentNode) return null;
      const idx = this.parentNode._children.indexOf(this);
      return idx > 0 ? this.parentNode._children[idx-1] : null;
    }
    insertBefore(child, ref) {
      if (child && child._fragment) {
        child._children.forEach(c => { if (c) { c.parentNode = this; c.parentElement = this; } });
        if (!ref) this._children.push(...child._children);
        else {
          const idx = this._children.indexOf(ref);
          if (idx < 0) this._children.push(...child._children);
          else this._children.splice(idx, 0, ...child._children);
        }
      } else if (child) {
        child.parentNode = this; child.parentElement = this;
        if (!ref) this._children.push(child);
        else {
          const idx = this._children.indexOf(ref);
          if (idx < 0) this._children.push(child);
          else this._children.splice(idx, 0, child);
        }
      }
      this._dirty = true;
      return child;
    }
    removeChild(child) {
      const idx = this._children.indexOf(child);
      if (idx >= 0) { this._children.splice(idx, 1); if (child) { child.parentNode = null; child.parentElement = null; } }
      this._dirty = true;
      return child;
    }
    insertAdjacentHTML(position, html) {
      position = String(position).toLowerCase();
      const parsed = document.createElement('div');
      parsed.innerHTML = html;
      const kids = parsed._children.slice();
      if (position === 'beforebegin') {
        if (this.parentNode) {
          kids.forEach(c => this.parentNode.insertBefore(c, this));
        }
      } else if (position === 'afterbegin') {
        kids.reverse().forEach(c => {
          if (this._children.length) this.insertBefore(c, this._children[0]);
          else this.appendChild(c);
        });
      } else if (position === 'beforeend') {
        kids.forEach(c => this.appendChild(c));
      } else if (position === 'afterend') {
        if (this.parentNode) {
          const idx = this.parentNode._children.indexOf(this);
          if (idx >= 0) {
            kids.forEach((c, i) => this.parentNode.insertBefore(c, this.parentNode._children[idx + 1 + i] || null));
          } else {
            kids.forEach(c => this.parentNode.appendChild(c));
          }
        }
      }
      this._dirty = true;
      return this;
    }
    insertAdjacentText(position, text) {
      const tn = new FenrirNode('#text');
      tn.textContent = String(text);
      if (position === 'beforebegin' || position === 'afterend') {
        if (this.parentNode) this.parentNode.insertBefore(tn, position === 'beforebegin' ? this : null);
      } else {
        this.insertAdjacentHTML(position, ''); // noop for text? simpler:
      }
      // simpler: just append text node appropriately
      return this;
    }
    insertAdjacentElement(position, el) {
      if (!el) return null;
      if (position === 'beforebegin' && this.parentNode) { this.parentNode.insertBefore(el, this); }
      else if (position === 'afterbegin') { this.insertBefore(el, this._children[0] || null); }
      else if (position === 'beforeend') { this.appendChild(el); }
      else if (position === 'afterend' && this.parentNode) {
        const idx = this.parentNode._children.indexOf(this);
        this.parentNode.insertBefore(el, this.parentNode._children[idx+1] || null);
      }
      this._dirty = true;
      return el;
    }
    append(...children) {
      children.forEach(c => this.appendChild(c));
      return this;
    }
    prepend(...children) {
      children.reverse().forEach(c => {
        if (this._children.length) this.insertBefore(c, this._children[0]);
        else this.appendChild(c);
      });
      return this;
    }
    add(child) {
      // for <select>: add option
      this.appendChild(child);
      return this;
    }
    get text() { return this.textContent; }
    set text(v) { this.textContent = v; }
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
    addEventListener(type, cb) {
      if (typeof cb !== 'function') return;
      this._listeners = this._listeners || {};
      this._listeners[type] = this._listeners[type] || [];
      if (!this._listeners[type].includes(cb)) this._listeners[type].push(cb);
    }
    removeEventListener(type, cb) {
      if (!this._listeners || !this._listeners[type]) return;
      this._listeners[type] = this._listeners[type].filter(x => x !== cb);
    }
    dispatchEvent(evt) {
      const type = (evt && evt.type) || '';
      const cbs = (this._listeners && this._listeners[type]) || [];
      cbs.slice().forEach(cb => { try { cb(evt); } catch (e) { console.error(e); } });
      return true;
    }
    querySelector(sel) {
      if (this.__handle) {
        const record = __querySelector(String(sel), this.__handle);
        if (record) return __wrapNativeElement(record);
      }
      const __match = (node, s) => {
        s = String(s).trim();
        if (!s) return false;
        if (s.startsWith('#')) return node.id === s.slice(1);
        if (s.startsWith('.')) return !!(node._classes && node._classes.has(s.slice(1)));
        // tag
        if (s.indexOf(' ') < 0 && s.indexOf('.') < 0 && s.indexOf('#') < 0) {
          return node.tagName.toLowerCase() === s.toLowerCase();
        }
        return false;
      };
      const walk = (node, s) => {
        for (const c of (node._children || [])) {
          if (__match(c, s)) return c;
          const r = walk(c, s);
          if (r) return r;
        }
        return null;
      };
      return walk(this, sel);
    }
    querySelectorAll(sel) {
      if (this.__handle) {
        return __querySelectorAll(String(sel), this.__handle).map(__wrapNativeElement);
      }
      const __match = (node, s) => {
        s = String(s).trim();
        if (!s) return false;
        if (s.startsWith('#')) return node.id === s.slice(1);
        if (s.startsWith('.')) return !!(node._classes && node._classes.has(s.slice(1)));
        if (s.indexOf(' ') < 0 && s.indexOf('.') < 0 && s.indexOf('#') < 0) {
          return node.tagName.toLowerCase() === s.toLowerCase();
        }
        return false;
      };
      const out = [];
      const walk = (node, s) => {
        for (const c of (node._children || [])) {
          if (__match(c, s)) out.push(c);
          walk(c, s);
        }
      };
      walk(this, sel);
      return out;
    }
    getElementsByTagName(tag) {
      return this.querySelectorAll(String(tag));
    }
    getElementsByClassName(cls) {
      return this.querySelectorAll('.' + String(cls));
    }
    getBoundingClientRect() { return {top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0}; }
    getClientRects() { return []; }
    focus() {}
  }
  function __matchSelector(node, selector) {
    selector = String(selector).trim();
    if (!selector) return false;
    if (selector.startsWith('#')) return node.id === selector.slice(1);
    if (selector.startsWith('.')) return !!(node._classes && node._classes.has(selector.slice(1)));
    if (selector.includes('.')) {
      const parts = selector.split('.');
      const tag = parts[0].toLowerCase();
      const cls = parts.slice(1).join('.');
      if (tag && node.tagName.toLowerCase() !== tag) return false;
      return !!(node._classes && node._classes.has(cls));
    }
    return node.tagName.toLowerCase() === selector.toLowerCase();
  }
  function __wrapNativeElement(record) {
    if (!record) return null;
    const handle = record.__handle;
    if (nodes[handle]) return nodes[handle];
    const element = new FenrirNode(record.tag || 'div', record.id || '');
    element.__handle = handle;
    for (const key of Object.keys(record)) {
      if (key === 'id' || key === 'tag' || key === '__handle') continue;
      element.setAttribute(key, record[key]);
    }
    nodes[handle] = element;
    return element;
  }
  const document = {
    documentElement: new FenrirNode('html'),
    body: new FenrirNode('body'),
    head: new FenrirNode('head'),
    readyState: 'loading',
    get cookie() { return __cookieGet(); },
    set cookie(v) { __cookieSet(v); },
    getElementById(id) {
      id = String(id);
      const native = __getElementById(id);
      if (native) return __wrapNativeElement(native);
      return nodes[id] || (nodes[id] = new FenrirNode('div', id));
    },
    createElement(tag) { return new FenrirNode(tag); },
    createTextNode(text) { const node = new FenrirNode('#text'); node.textContent = text; return node; },
    createDocumentFragment() { return new FenrirNode('#fragment', '', true); },
    querySelector(selector) {
      const r = __querySelector(String(selector));
      if (r) return __wrapNativeElement(r);
      // Fallback: search JS-created (detached) nodes.
      for (const n of __allNodes) {
        const res = n.querySelector ? n.querySelector(selector) : null;
        if (res) return res;
      }
      return null;
    },
    querySelectorAll(selector) {
      const res = __querySelectorAll(String(selector));
      return res.map(__wrapNativeElement);
    },
    getElementsByTagName(tag) {
      const res = this.querySelectorAll(String(tag));
      if (res.length === 0 && String(tag).toLowerCase() === 'script') {
        return [document.documentElement || this.documentElement];
      }
      return res;
    },
    getElementsByClassName(cls) {
      return this.querySelectorAll('.' + String(cls));
    },
    defaultView: null,
    addEventListener(type, cb) {
      if (typeof cb !== 'function') return;
      this._listeners = this._listeners || {};
      this._listeners[type] = this._listeners[type] || [];
      if (!this._listeners[type].includes(cb)) this._listeners[type].push(cb);
    },
    removeEventListener(type, cb) {
      if (!this._listeners || !this._listeners[type]) return;
      this._listeners[type] = this._listeners[type].filter(x => x !== cb);
    },
    dispatchEvent(evt) {
      const type = (evt && evt.type) || '';
      const cbs = (this._listeners && this._listeners[type]) || [];
      cbs.slice().forEach(cb => { try { cb(evt); } catch (e) { console.error(e); } });
      return true;
    }
  };
  document.documentElement = document.querySelector('html') || document.documentElement;
  document.body = document.querySelector('body') || document.body;
  document.head = document.querySelector('head') || document.head;
  globalThis.document = document;
  // window is the global object itself (like browsers), so `window.X = ...`
  // creates a global X. This is required by libraries (modernizr) that do
  // `window.Modernizr = ...` then reference `Modernizr` globally.
  if (!globalThis.window || globalThis.window !== globalThis) {
    // merge any existing window props into globalThis
    const w = globalThis.window || {};
    for (const k of Object.keys(w)) { try { globalThis[k] = w[k]; } catch (e) {} }
    globalThis.window = globalThis;
  }
  globalThis.window.innerWidth = 1024;
  globalThis.window.innerHeight = 768;
  globalThis.window.scrollX = 0;
  globalThis.window.scrollY = 0;
  globalThis.window._listeners = globalThis.window._listeners || {};
  globalThis.window.addEventListener = function(type, cb) {
    if (typeof cb !== 'function') return;
    this._listeners = this._listeners || {};
    this._listeners[type] = this._listeners[type] || [];
    if (!this._listeners[type].includes(cb)) this._listeners[type].push(cb);
  };
  globalThis.window.removeEventListener = function(type, cb) {
    if (!this._listeners || !this._listeners[type]) return;
    this._listeners[type] = this._listeners[type].filter(x => x !== cb);
  };
  globalThis.window.dispatchEvent = function(evt) {
    const type = (evt && evt.type) || '';
    const cbs = (this._listeners && this._listeners[type]) || [];
    cbs.slice().forEach(cb => { try { cb(evt); } catch (e) { console.error(e); } });
    return true;
  };
  globalThis.window.scrollTo = function(x, y) { this.scrollX = x || 0; this.scrollY = y || 0; };
  globalThis.window.scrollBy = function(x, y) { this.scrollX += x || 0; this.scrollY += y || 0; };
  globalThis.window.matchMedia = () => ({matches: true, addEventListener() {}, addListener(cb) { if (typeof cb === 'function') setTimeout(cb, 0); }, removeListener() {}});
  function __parseUrl(href) {
    href = String(href || '');
    let protocol = '', host = '', hostname = '', port = '', pathname = '/', search = '', hash = '';
    const hashIdx = href.indexOf('#');
    if (hashIdx >= 0) { hash = href.slice(hashIdx); href = href.slice(0, hashIdx); }
    const qIdx = href.indexOf('?');
    if (qIdx >= 0) { search = href.slice(qIdx); href = href.slice(0, qIdx); }
    const schemeIdx = href.indexOf('://');
    if (schemeIdx >= 0) {
      protocol = href.slice(0, schemeIdx + 1) + '//';
      const rest = href.slice(schemeIdx + 3);
      const slashIdx = rest.indexOf('/');
      const authority = slashIdx >= 0 ? rest.slice(0, slashIdx) : rest;
      const path = slashIdx >= 0 ? rest.slice(slashIdx) : '';
      const colonIdx = authority.indexOf(':');
      if (colonIdx >= 0) { hostname = authority.slice(0, colonIdx); port = authority.slice(colonIdx + 1); }
      else hostname = authority;
      host = authority;
      pathname = path || '/';
      protocol = protocol.replace('//', ':').replace(':', ':') ;
      // fix protocol to include //
      protocol = href.slice(0, schemeIdx) + ':';
    } else {
      // relative or path-only
      if (href.startsWith('/')) pathname = href || '/';
      else if (href) pathname = '/' + href;
    }
    if (!protocol) protocol = 'https:';
    if (!host) host = '';
    return { href: (typeof __pageUrl === 'string' ? __pageUrl : ''), protocol, host, hostname, port, pathname, search, hash };
  }
  const __initialHref = (typeof __pageUrl === 'string' && __pageUrl) ? __pageUrl : '';
  const __loc = __parseUrl(__initialHref);
  __loc.href = __initialHref;
  document.defaultView = window;
  window.document = document;
  globalThis.location = __loc;
  // Make location.href a live-ish property via accessor is complex; provide assign/replace/reload
  globalThis.location.assign = (u) => { globalThis.location.href = String(u); };
  globalThis.location.replace = (u) => { globalThis.location.href = String(u); };
  globalThis.location.reload = () => {};
  globalThis.location.toString = () => globalThis.location.href;

  // History API (in-memory)
  const __historyStack = [{url: __initialHref, state: null}];
  let __historyIndex = 0;
  globalThis.history = {
    get length() { return __historyStack.length; },
    get state() { return __historyStack[__historyIndex] ? __historyStack[__historyIndex].state : null; },
    pushState(state, title, url) {
      if (url) {
        __historyStack.splice(__historyIndex + 1);
        __historyStack.push({state: state, url: String(url)});
        __historyIndex = __historyStack.length - 1;
        globalThis.location.href = String(url);
        globalThis.location.pathname = String(url);
      } else {
        if (__historyStack[__historyIndex]) __historyStack[__historyIndex].state = state;
      }
    },
    replaceState(state, title, url) {
      if (__historyStack[__historyIndex]) {
        __historyStack[__historyIndex].state = state;
        if (url) { __historyStack[__historyIndex].url = String(url); globalThis.location.href = String(url); }
      }
    },
    back() { if (__historyIndex > 0) { __historyIndex--; const e = {type:'popstate', state: this.state, target: window}; window.dispatchEvent(e); } },
    forward() { if (__historyIndex < __historyStack.length - 1) { __historyIndex++; const e = {type:'popstate', state: this.state, target: window}; window.dispatchEvent(e); } },
    go(delta) { const ni = __historyIndex + (delta||0); if (ni >= 0 && ni < __historyStack.length) { __historyIndex = ni; const e = {type:'popstate', state: this.state, target: window}; window.dispatchEvent(e); } }
  };

  globalThis.navigator = {
    userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
    platform: 'Linux x86_64', language: 'en-US', languages: ['en-US', 'en'],
    onLine: true, cookieEnabled: false, hardwareConcurrency: 4, maxTouchPoints: 0
  };
  let __timerSeq = 1;
  globalThis.setTimeout = (fn, delay) => {
    const id = __timerSeq++;
    if (typeof fn === 'function') __taskQueue.push(() => { try { fn(); } catch (e) { console.error(e); } });
    return id;
  };
  globalThis.setInterval = (fn, delay) => globalThis.setTimeout(fn, delay);
  globalThis.clearTimeout = (id) => {};
  globalThis.clearInterval = (id) => {};
  globalThis.requestAnimationFrame = (fn) => {
    const id = __timerSeq++;
    if (typeof fn === 'function') __taskQueue.push(() => { try { fn(Date.now()); } catch (e) { console.error(e); } });
    return id;
  };
  globalThis.IntersectionObserver = class {
    constructor(cb, options) { this._cb = (typeof cb === 'function') ? cb : () => {}; this._options = options || {}; this._targets = []; }
    observe(target) {
      if (!target) return;
      this._targets.push(target);
      const self = this;
      __taskQueue.push(() => {
        try {
          const entries = self._targets.map(t => ({
            target: t, isIntersecting: true, intersectionRatio: 1,
            boundingClientRect: { top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 },
            intersectionRect: { top: 0, bottom: 0, left: 0, right: 0, width: 0, height: 0 },
            rootBounds: null, time: Date.now()
          }));
          self._cb(entries, self);
        } catch (e) { console.error(e); }
      });
    }
    unobserve(target) { this._targets = this._targets.filter(t => t !== target); }
    disconnect() { this._targets = []; }
    takeRecords() { return []; }
  };
  globalThis.MutationObserver = class { constructor(cb) {} observe() {} disconnect() {} takeRecords() { return []; } };
  globalThis.console = {log(){}, info(){}, warn(){}, error(...a){ let msg = a.map(x => (x && x.stack) ? (x.message + '\\n' + x.stack) : String(x)).join(' '); if (typeof __errLog === 'function') __errLog(msg); }};
  globalThis.__fenrirNodes = nodes;
  globalThis.Event = class Event {
    constructor(type, init) { this.type = String(type); if (init) Object.assign(this, init); }
  };
  globalThis.CustomEvent = globalThis.Event;
  globalThis.localStorage = __makeStorage();
  globalThis.sessionStorage = __makeStorage();
  globalThis.fetch = (url, options) => {
    return new Promise((resolve, reject) => {
      try {
        const r = __fetchSync(String(url));
        if (r.error) { reject(new Error(r.error)); return; }
        const response = {
          ok: r.status >= 200 && r.status < 300,
          status: r.status, statusText: '', url: String(url),
          headers: new Map(),
          text: () => Promise.resolve(r.body),
          json: () => { try { return Promise.resolve(JSON.parse(r.body)); } catch (e) { return Promise.reject(e); } },
          clone: function() { return this; }
        };
        resolve(response);
      } catch (e) { reject(e); }
    });
  };
  globalThis.__drainTasks = () => {
    let guard = 0;
    while (__taskQueue.length && guard < 1000) {
      const batch = __taskQueue.splice(0, __taskQueue.length);
      for (const t of batch) { try { t(); } catch (e) { console.error(e); } }
      guard++;
    }
  };
  globalThis.__dispatchReady = () => {
    try { document.readyState = 'complete'; } catch (e) {}
    const makeEvt = (type) => ({ type, target: document, currentTarget: document, bubbles: false, cancelable: false, defaultPrevented: false });
    try { document.dispatchEvent(makeEvt('DOMContentLoaded')); } catch (e) { console.error(e); }
    try { window.dispatchEvent({ type: 'load', target: window, currentTarget: window }); } catch (e) { console.error(e); }
  };

  // atob / btoa
  globalThis.atob = (str) => {
    str = String(str).replace(/\s+/g, '');
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let out = '', i = 0;
    const lookup = Object.create(null);
    for (let j = 0; j < chars.length; j++) lookup[chars[j]] = j;
    while (i < str.length) {
      const a = lookup[str[i++]] || 0;
      const b = i < str.length ? (lookup[str[i++]] || 0) : 0;
      const c = i < str.length ? (lookup[str[i++]] || 0) : 0;
      const d = i < str.length ? (lookup[str[i++]] || 0) : 0;
      const n = (a << 18) | (b << 12) | (c << 6) | d;
      out += String.fromCharCode((n >> 16) & 255);
      if (str[i-2] !== '=') out += String.fromCharCode((n >> 8) & 255);
      if (str[i-1] !== '=') out += String.fromCharCode(n & 255);
    }
    return out;
  };
  globalThis.btoa = (str) => {
    str = String(str);
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let out = '';
    for (let i = 0; i < str.length; i += 3) {
      const a = str.charCodeAt(i);
      const b = i+1 < str.length ? str.charCodeAt(i+1) : 0;
      const c = i+2 < str.length ? str.charCodeAt(i+2) : 0;
      const n = (a << 16) | (b << 8) | c;
      out += chars[(n >> 18) & 63];
      out += chars[(n >> 12) & 63];
      out += i+1 < str.length ? chars[(n >> 6) & 63] : '=';
      out += i+2 < str.length ? chars[n & 63] : '=';
    }
    return out;
  };

  // URLSearchParams
  globalThis.URLSearchParams = class URLSearchParams {
    constructor(init) {
      this._params = [];
      if (typeof init === 'string') {
        const q = init.startsWith('?') ? init.slice(1) : init;
        q.split('&').forEach(pair => {
          if (!pair) return;
          const idx = pair.indexOf('=');
          const k = decodeURIComponent((idx>=0?pair.slice(0,idx):pair).replace(/\+/g,' '));
          const v = idx>=0 ? decodeURIComponent(pair.slice(idx+1).replace(/\+/g,' ')) : '';
          this._params.push([k, v]);
        });
      } else if (init && typeof init.forEach === 'function') {
        init.forEach((v,k) => this._params.push([String(k), String(v)]));
      }
    }
    append(k,v){ this._params.push([String(k), String(v)]); }
    delete(k){ this._params = this._params.filter(p => p[0] !== String(k)); }
    get(k){ const p = this._params.find(p => p[0] === String(k)); return p ? p[1] : null; }
    getAll(k){ return this._params.filter(p => p[0] === String(k)).map(p => p[1]); }
    has(k){ return this._params.some(p => p[0] === String(k)); }
    set(k,v){
      const ks = String(k);
      let found = false;
      this._params = this._params.filter(p => { if (p[0] === ks) { if (!found) { found = true; return true; } return false; } return true; });
      const p = this._params.find(p => p[0] === ks);
      if (p) p[1] = String(v); else this._params.push([ks, String(v)]);
    }
    toString(){ return this._params.map(p => encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1])).join('&'); }
    forEach(cb){ this._params.forEach(p => cb(p[1], p[0])); }
    entries(){ return this._params[Symbol.iterator](); }
    keys(){ return this._params.map(p => p[0])[Symbol.iterator](); }
    values(){ return this._params.map(p => p[1])[Symbol.iterator](); }
  };

  // URL
  globalThis.URL = class URL {
    constructor(url, base) {
      let href = String(url);
      if (base && !href.match(/^[a-z]+:/i)) {
        // resolve against base
        try {
          const b = (base instanceof URL) ? base : new URL(base);
          if (href.startsWith('/')) {
            href = b.protocol + '//' + b.host + href;
          } else {
            const path = b.pathname.replace(/\/[^\/]*$/, '/');
            href = b.protocol + '//' + b.host + path + href;
          }
        } catch(e) {}
      }
      this.href = href;
      // parse
      let protocol='', host='', hostname='', port='', pathname='/', search='', hash='';
      let h = href;
      const hi = h.indexOf('#'); if (hi>=0){ hash=h.slice(hi); h=h.slice(0,hi); }
      const qi = h.indexOf('?'); if (qi>=0){ search=h.slice(qi); h=h.slice(0,qi); }
      const si = h.indexOf('://');
      if (si>=0) {
        protocol = h.slice(0, si+1);
        const rest = h.slice(si+3);
        const slash = rest.indexOf('/');
        const auth = slash>=0 ? rest.slice(0,slash) : rest;
        const path = slash>=0 ? rest.slice(slash) : '/';
        const ci = auth.indexOf(':');
        if (ci>=0){ hostname=auth.slice(0,ci); port=auth.slice(ci+1); } else hostname=auth;
        host = auth; pathname = path || '/';
      } else {
        pathname = h || '/';
      }
      this.protocol = protocol || 'https:';
      this.host = host;
      this.hostname = hostname || host;
      this.port = port;
      this.pathname = pathname;
      this.search = search;
      this.hash = hash;
      this.origin = host ? (this.protocol + '//' + host) : '';
      this.searchParams = new URLSearchParams(search);
    }
    toString(){ return this.href; }
    toJSON(){ return this.href; }
  };

  // XMLHttpRequest (basic, synchronous)
  globalThis.XMLHttpRequest = class XMLHttpRequest {
    constructor() {
      this.readyState = 0; this.status = 0; this.statusText = '';
      this.responseText = ''; this.response = ''; this.responseType = '';
      this._listeners = {}; this._headers = {};
      this.onreadystatechange = null; this.onload = null; this.onerror = null;
    }
    open(method, url, async) {
      this._method = method; this._url = url; this._async = (async !== false);
      this.readyState = 1;
      this._fire('readystatechange');
    }
    setRequestHeader(k,v){ this._headers[k]=v; }
    send(body) {
      this.readyState = 2; this._fire('readystatechange');
      try {
        const r = __fetchSync(String(this._url));
        this.status = r.status; this.statusText = '';
        this.responseText = r.body; this.response = r.body;
        this.readyState = 4;
        this._fire('readystatechange');
        if (this.onload) try { this.onload(); } catch(e){ console.error(e); }
        this._fire('load');
      } catch (e) {
        this.readyState = 4;
        if (this.onerror) try { this.onerror(e); } catch(err){ console.error(err); }
        this._fire('error');
      }
    }
    abort(){ this.readyState = 0; }
    addEventListener(type, cb) {
      if (typeof cb !== 'function') return;
      this._listeners[type] = this._listeners[type] || [];
      if (!this._listeners[type].includes(cb)) this._listeners[type].push(cb);
    }
    removeEventListener(type, cb) {
      if (!this._listeners[type]) return;
      this._listeners[type] = this._listeners[type].filter(x => x !== cb);
    }
    _fire(type) {
      const cbs = (this._listeners[type] || []).slice();
      cbs.forEach(cb => { try { cb({type, target: this}); } catch(e){ console.error(e); } });
      if (type === 'readystatechange' && typeof this.onreadystatechange === 'function') {
        try { this.onreadystatechange(); } catch(e){ console.error(e); }
      }
    }
    getAllResponseHeaders(){ return ''; }
    getResponseHeader(){ return null; }
  };

  globalThis.getComputedStyle = (el) => {
    const style = (el && el.style) ? el.style : {};
    return new Proxy(style, {
      get(target, prop) {
        if (prop === 'getPropertyValue') return (name) => (target[name] || '');
        if (typeof prop === 'string') return target[prop] || '';
        return target[prop];
      }
    });
  };
  globalThis.Element = FenrirNode;
  globalThis.HTMLElement = FenrirNode;
  globalThis.Node = FenrirNode;
  globalThis.Image = function(w, h) {
    const img = document.createElement('img');
    if (w) img.width = w;
    if (h) img.height = h;
    return img;
  };
  globalThis.__fenrirTakePatches = () => {
    const result = Object.values(nodes)
      .filter(node => node._dirty && node.id)
      .map(node => ({id: node.id, html: node.innerHTML}));
    Object.values(nodes).forEach(node => { node._dirty = false; });
    return result;
  };
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
                                    std::vector<BrowserScriptRuntime::DomPatch>& patches,
                                    std::string& error) const {
    patches.clear();
    error.clear();
    if (!document || !document->root()) return true;

    BrowserScriptContext ctx;
    if (!ctx.initialize(document, pageUrl, patches, error)) return false;
    return true;
}



BrowserScriptContext::BrowserScriptContext() = default;

BrowserScriptContext::~BrowserScriptContext() { teardown(); }

std::string BrowserScriptContext::registerElement(const litehtml::element::ptr& element) {
    if (!element) return {};
    if (const auto existing = elementHandleByPointer_.find(element.get());
        existing != elementHandleByPointer_.end()) {
        return existing->second;
    }
    const char* existingId = element ? element->get_attr("id") : nullptr;
    const std::string handle = "__fenrir_handle_" + std::to_string(++nextElementHandle_);
    if (!existingId || !*existingId) {
        const std::string syntheticId = "__fenrir_element_" + std::to_string(nextElementHandle_);
        element->set_attr("id", syntheticId.c_str());
    }
    elementHandles_[handle] = element;
    elementHandleByPointer_[element.get()] = handle;
    return handle;
}

litehtml::element::ptr BrowserScriptContext::resolveElement(const std::string& id) const {
    const auto it = elementHandles_.find(id);
    return it == elementHandles_.end() ? litehtml::element::ptr{} : it->second;
}

litehtml::element::ptr BrowserScriptContext::findElementById(const std::string& id) const {
    if (!document_ || !document_->root()) return {};
    litehtml::element::ptr found;
    std::function<void(const litehtml::element::ptr&)> visit = [&](const litehtml::element::ptr& element) {
        if (!element || found) return;
        const char* elementId = element->get_attr("id");
        if (elementId && id == elementId) {
            found = element;
            return;
        }
        for (const auto& child : element->children()) visit(child);
    };
    visit(document_->root());
    return found;
}

bool BrowserScriptContext::setupRuntime(const std::string& pageUrl, std::string& error) {
    pageUrl_ = pageUrl;
    auto* host = new RuntimeHost(pageUrl);
    host_ = host;
    runtime_ = JS_NewRuntime();
    if (!runtime_) { error = "Could not create JavaScript runtime."; delete host; host_ = nullptr; return false; }
    JS_SetMemoryLimit(runtime_, kRuntimeMemoryLimit);
    JS_SetMaxStackSize(runtime_, 8 * 1024 * 1024);
    host->deadline = std::chrono::steady_clock::now() + kScriptBudget;
    JS_SetInterruptHandler(runtime_, interruptRuntime, host);
    JS_SetModuleLoaderFunc(runtime_, normalizeModule, loadModule, host);

    context_ = JS_NewContext(runtime_);
    if (!context_) { teardown(); error = "Could not create JavaScript context."; return false; }
    JS_SetContextOpaque(context_, this);

    {
        JSValue global = JS_GetGlobalObject(context_);
        JS_SetPropertyStr(context_, global, "__pageUrl", JS_NewString(context_, pageUrl.c_str()));
        JSValue fn = JS_NewCFunction(context_, jsFetchSync, "__fetchSync", 1);
        JS_SetPropertyStr(context_, global, "__fetchSync", fn);
        JSValue cg = JS_NewCFunction(context_, jsCookieGet, "__cookieGet", 0);
        JS_SetPropertyStr(context_, global, "__cookieGet", cg);
        JSValue cs = JS_NewCFunction(context_, jsCookieSet, "__cookieSet", 1);
        JS_SetPropertyStr(context_, global, "__cookieSet", cs);
        JSValue qs = JS_NewCFunction(context_, jsQuerySelector, "__querySelector", 2);
        JS_SetPropertyStr(context_, global, "__querySelector", qs);
        JSValue qsa = JS_NewCFunction(context_, jsQuerySelectorAll, "__querySelectorAll", 2);
        JS_SetPropertyStr(context_, global, "__querySelectorAll", qsa);
        JSValue byId = JS_NewCFunction(context_, jsGetElementById, "__getElementById", 1);
        JS_SetPropertyStr(context_, global, "__getElementById", byId);
        JSValue el = JS_NewCFunction(context_, [](JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)->JSValue {
            if (argc>0) {
                const char* m = JS_ToCString(ctx, argv[0]);
                if (m) {
                    auto* c = static_cast<BrowserScriptContext*>(JS_GetContextOpaque(ctx));
                    if (c) c->recordError(m);
                    JS_FreeCString(ctx, m);
                }
            }
            return JS_UNDEFINED;
        }, "__errLog", 1);
        JS_SetPropertyStr(context_, global, "__errLog", el);
        JS_FreeValue(context_, global);
    }

    bool success = evaluate(context_, *host, kDomShim, "fenrir:dom", false, error);
    if (!success && error.empty()) error = "DOM shim failed.";
    return true;
}

void BrowserScriptContext::teardown() {
    if (context_) { JS_FreeContext(context_); context_ = nullptr; }
    if (runtime_) { JS_FreeRuntime(runtime_); runtime_ = nullptr; }
    if (host_) { delete static_cast<RuntimeHost*>(host_); host_ = nullptr; }
    elementHandles_.clear();
    elementHandleByPointer_.clear();
}

bool BrowserScriptContext::runScripts(const litehtml::document::ptr& document, std::string& error) {
    if (!context_) return false;
    auto* host = static_cast<RuntimeHost*>(host_);
    std::vector<litehtml::element::ptr> scripts;
    std::function<void(const litehtml::element::ptr&)> collect = [&](const litehtml::element::ptr& element) {
        if (std::string(element->get_tagName()) == "script") scripts.push_back(element);
        for (const auto& child : element->children()) collect(child);
    };
    collect(document->root());

    int inlineIndex = 0;
    bool success = true;
    for (const auto& script : scripts) {
        const char* typeAttr = script->get_attr("type");
        const std::string type = typeAttr ? typeAttr : "";
        if (type == "application/ld+json") continue;
        if (type == "importmap") continue;
        if (type == "application/json") continue;
        if (type == "text/template") continue;
        const bool module = type == "module";
        const char* srcAttr = script->get_attr("src");
        std::string filename = pageUrl_ + "#inline-" + std::to_string(inlineIndex++);
        std::string source;
        if (srcAttr && *srcAttr) {
            filename = browser::resolveUrl(pageUrl_, srcAttr);
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
        if (!evaluate(context_, *host, source, filename, module, scriptError)) {
            success = false;
            if (error.empty()) error = filename + ": " + scriptError;
        }
    }
    std::string e;
    (void)evaluate(context_, *host, "if(typeof __drainTasks==='function')__drainTasks();", "fenrir:drain", false, e);
    (void)evaluate(context_, *host, "if(typeof __dispatchReady==='function')__dispatchReady();", "fenrir:ready", false, e);
    if (!e.empty() && error.empty()) error = e;
    return success;
}

bool BrowserScriptContext::collectPatches(std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error) {
    (void)error;
    patches.clear();
    if (!context_) return false;
    JSValue global = JS_GetGlobalObject(context_);
    JSValue nodes = JS_GetPropertyStr(context_, global, "__fenrirNodes");
    JSPropertyEnum* properties = nullptr;
    uint32_t propertyCount = 0;
    if (JS_IsObject(nodes) &&
        JS_GetOwnPropertyNames(context_, &properties, &propertyCount, nodes,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t i = 0; i < propertyCount; ++i) {
            const char* idValue = JS_AtomToCString(context_, properties[i].atom);
            if (!idValue) continue;
            JS_FreeCString(context_, idValue);
            JSValue node = JS_GetProperty(context_, nodes, properties[i].atom);
            JSValue dirty = JS_GetPropertyStr(context_, node, "_dirty");
            if (JS_ToBool(context_, dirty) > 0) {
                JSValue nodeId = JS_GetPropertyStr(context_, node, "id");
                JSValue html = JS_GetPropertyStr(context_, node, "innerHTML");
                const std::string id = getString(context_, nodeId);
                if (!id.empty()) {
                    patches.push_back(BrowserScriptRuntime::DomPatch{ id, getString(context_, html) });
                }
                JS_FreeValue(context_, nodeId);
                JS_FreeValue(context_, html);
            }
            // Clear the dirty flag so subsequent dispatches don't re-emit.
            JS_SetPropertyStr(context_, node, "_dirty", JS_NewBool(context_, 0));
            JS_FreeValue(context_, dirty);
            JS_FreeValue(context_, node);
        }
        JS_FreePropertyEnum(context_, properties, propertyCount);
    }
    JS_FreeValue(context_, nodes);
    JS_FreeValue(context_, global);
    return true;
}

bool BrowserScriptContext::initialize(const litehtml::document::ptr& document,
                                     const std::string& pageUrl,
                                     std::vector<BrowserScriptRuntime::DomPatch>& patches,
                                     std::string& error) {
    patches.clear();
    if (!document || !document->root()) return true;
    document_ = document;
    if (!setupRuntime(pageUrl, error)) return false;
    std::string scriptError;
    (void)runScripts(document, scriptError);
    collectPatches(patches, error);
    if (!scriptError.empty() && error.empty()) error = scriptError;
    return true;
}

bool BrowserScriptContext::dispatchEvent(const std::string& targetId, const std::string& type,
                                       std::vector<BrowserScriptRuntime::DomPatch>& patches, std::string& error) {
    patches.clear();
    if (!context_) return false;
    auto* host = static_cast<RuntimeHost*>(host_);
    std::string code = "var __t = (typeof __fenrirNodes!=='undefined' && " +
        std::string(targetId.empty() ? "null" : ("__fenrirNodes['" + targetId + "']")) +
        ") || document; if (__t && typeof __t.dispatchEvent==='function') __t.dispatchEvent({type:'" + type +
        "', target:__t, bubbles:true, cancelable:true}); if (typeof __drainTasks==='function') __drainTasks();";
    std::string e;
    (void)evaluate(context_, *host, code, "fenrir:dispatch", false, e);
    collectPatches(patches, error);
    if (!e.empty() && error.empty()) error = e;
    return true;
}

bool BrowserScriptContext::evaluateSnippet(const std::string& code, std::string& error) {
    if (!context_) return false;
    auto* host = static_cast<RuntimeHost*>(host_);
    return evaluate(context_, *host, code, "fenrir:snippet", false, error);
}

std::string BrowserScriptContext::evalString(const std::string& code, std::string& error) {
    if (!context_) return {};
    auto* host = static_cast<RuntimeHost*>(host_);
    JSValue result = JS_Eval(context_, code.c_str(), code.size(), "fenrir:evalstr", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        error = exceptionText(context_);
        return {};
    }
    const char* str = JS_ToCString(context_, result);
    std::string out = str ? str : "";
    if (str) JS_FreeCString(context_, str);
    JS_FreeValue(context_, result);
    (void)host;
    return out;
}

} // namespace fenrir::apps::browser
