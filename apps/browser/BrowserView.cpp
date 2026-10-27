#include "BrowserView.hpp"

#include <algorithm>
#include <utility>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Prefs.hpp"

namespace fenrir::apps::browser {

BrowserView::BrowserView(AppContext& context) : context_(context) {
    tabBar_.onSwitch = [this](int index) { selectTab(index); };

    auto back = std::make_unique<Button>(U"Back");
    backButton_ = back.get();
    backButton_->onActivate = [this] { goBack(); };
    toolbar_.addFixed(std::move(back), 6);

    auto forward = std::make_unique<Button>(U"Forward");
    forwardButton_ = forward.get();
    forwardButton_->onActivate = [this] { goForward(); };
    toolbar_.addFixed(std::move(forward), 8);

    auto reloadButton = std::make_unique<Button>(U"Reload");
    reloadButton_ = reloadButton.get();
    reloadButton_->onActivate = [this] { reload(); };
    toolbar_.addFixed(std::move(reloadButton), 7);

    auto homeButton = std::make_unique<Button>(U"Home");
    homeButton_ = homeButton.get();
    homeButton_->onActivate = [this] { goHome(); };
    toolbar_.addFixed(std::move(homeButton), 6);

    auto addTabButton = std::make_unique<Button>(U"+");
    newTabButton_ = addTabButton.get();
    newTabButton_->onActivate = [this] { newTab(); };
    toolbar_.addFixed(std::move(addTabButton), 3);

    auto address = std::make_unique<LineEdit>();
    address_ = address.get();
    address_->onSubmit = [this](const std::u32string& value) {
        navigateCurrent(text::toUtf8(value));
        toolbarActive_ = false;
    };
    addressSlot_ = toolbar_.addFlexible(std::move(address));
    toolbar_.focus(addressSlot_);

    newTab();
    toolbarActive_ = false;
}

BrowserView::Tab* BrowserView::activeTab() {
    return active_ >= 0 && static_cast<std::size_t>(active_) < tabs_.size()
               ? &tabs_[static_cast<std::size_t>(active_)] : nullptr;
}

const BrowserView::Tab* BrowserView::activeTab() const {
    return active_ >= 0 && static_cast<std::size_t>(active_) < tabs_.size()
               ? &tabs_[static_cast<std::size_t>(active_)] : nullptr;
}

BrowserPage* BrowserView::activePage() {
    Tab* tab = activeTab();
    return tab ? tab->page.get() : nullptr;
}

const BrowserPage* BrowserView::activePage() const {
    const Tab* tab = activeTab();
    return tab ? tab->page.get() : nullptr;
}

void BrowserView::newTab(std::string url) {
    Tab tab;
    tab.page = std::make_unique<BrowserPage>();
    tabs_.push_back(std::move(tab));
    active_ = static_cast<int>(tabs_.size()) - 1;
    syncTabs();
    toolbar_.focus(addressSlot_);
    toolbarActive_ = false;
    if (url.empty()) {
        auto& p = prefs::forApp("browser");
        url = p.get("homepage", "");
    }
    address_->setValue(text::toUtf32(url));
    if (!url.empty()) navigateCurrent(url);
    else if (auto* page = activePage())
        page->loadHtml("<!doctype html><html><body><h2>New Tab</h2><p>Use Ctrl+L to enter an address.</p></body></html>", "");
    syncToolbar();
}

void BrowserView::closeTab() {
    if (tabs_.size() <= 1) {
        toolbarActive_ = false;
        tabs_[0].history.clear();
        tabs_[0].historyIndex = -1;
        (void)tabs_[0].page->loadHtml(
            "<!doctype html><html><body><h2>New Tab</h2><p>Use Ctrl+L to enter an address.</p></body></html>", "");
        address_->setValue(U"");
        syncTabs();
        syncToolbar();
        return;
    }
    tabs_.erase(tabs_.begin() + active_);
    active_ = std::min(active_, static_cast<int>(tabs_.size()) - 1);
    syncTabs();
    selectTab(active_);
}

void BrowserView::navigateCurrent(const std::string& address, bool addHistory) {
    Tab* tab = activeTab();
    if (!tab || address.empty()) return;
    const std::string url = browser::normalizeUrl(address);
    if (url.empty()) {
        tab->page->navigate(address);
        return;
    }
    if (addHistory) {
        if (tab->historyIndex >= 0 &&
            tab->history[static_cast<std::size_t>(tab->historyIndex)] == url) {
            // Reloading the current URL does not add a duplicate history entry.
        } else {
            tab->history.erase(tab->history.begin() + tab->historyIndex + 1, tab->history.end());
            tab->history.push_back(url);
            tab->historyIndex = static_cast<int>(tab->history.size()) - 1;
        }
    }
    address_->setValue(text::toUtf32(url));
    tab->page->navigate(url);
    syncTabs();
    syncToolbar();
}

void BrowserView::goBack() {
    Tab* tab = activeTab();
    if (!tab || tab->historyIndex <= 0) return;
    --tab->historyIndex;
    const auto& url = tab->history[static_cast<std::size_t>(tab->historyIndex)];
    address_->setValue(text::toUtf32(url));
    tab->page->navigate(url);
    syncToolbar();
}

void BrowserView::goForward() {
    Tab* tab = activeTab();
    if (!tab || tab->historyIndex + 1 >= static_cast<int>(tab->history.size())) return;
    ++tab->historyIndex;
    const auto& url = tab->history[static_cast<std::size_t>(tab->historyIndex)];
    address_->setValue(text::toUtf32(url));
    tab->page->navigate(url);
    syncToolbar();
}

void BrowserView::reload() {
    if (auto* page = activePage(); page && !page->url().empty())
        navigateCurrent(page->url(), false);
}

void BrowserView::waitForLoad() {
    if (auto* page = activePage()) page->waitForLoad();
}

std::string BrowserView::currentUrl() const {
    const auto* page = activePage();
    return page ? page->url() : std::string{};
}

void BrowserView::goHome() {
    auto& p = prefs::forApp("browser");
    std::string home = p.get("homepage", "");
    if (home.empty()) home = "https://example.com";
    navigateCurrent(home);
}

void BrowserView::openFocusedLinkInNewTab() {
    if (auto* page = activePage(); page && !page->focusedLinkUrl().empty())
        newTab(page->focusedLinkUrl());
}

void BrowserView::selectTab(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= tabs_.size()) return;
    active_ = index;
    tabBar_.select(active_);
    const Tab* tab = activeTab();
    address_->setValue(tab && !tab->page->url().empty()
                           ? text::toUtf32(tab->page->url()) : std::u32string{});
    toolbarActive_ = false;
    syncToolbar();
}

void BrowserView::syncTabs() {
    std::vector<TabBar::Tab> labels;
    labels.reserve(tabs_.size());
    bool changed = false;
    for (auto& tab : tabs_) {
        const std::string title = tab.page->title().empty() ? "New Tab" : tab.page->title();
        if (title != tab.shownTitle) { tab.shownTitle = title; changed = true; }
        labels.push_back(TabBar::Tab{text::toUtf32(title), false});
    }
    if (changed || tabBar_.count() != static_cast<int>(labels.size())) {
        tabBar_.setTabs(std::move(labels));
        tabBar_.select(active_);
    }
}

void BrowserView::syncToolbar() {
    const Tab* tab = activeTab();
    backButton_->setEnabled(tab && tab->historyIndex > 0);
    forwardButton_->setEnabled(tab && tab->historyIndex + 1 < static_cast<int>(tab->history.size()));
}

void BrowserView::followPendingLink() {
    if (auto* page = activePage()) {
        const std::string target = page->takePendingNavigation();
        if (!target.empty()) navigateCurrent(target);
    }
}

void BrowserView::draw(Screen& screen) {
    if (!titleReady_) {
        context_.setWindowTitle(activePage() && !activePage()->title().empty()
                                    ? activePage()->title() : "New Tab");
        titleReady_ = true;
    }
    const int tabHeight = 1;
    const int toolbarHeight = 1;
    const int statusHeight = 1;
    const int pageHeight = std::max(0, bounds_.h - tabHeight - toolbarHeight - statusHeight);

    tabBar_.setBounds(Rect{bounds_.x, bounds_.y, bounds_.w, tabHeight});
    tabBar_.setFocused(focused_);
    tabBar_.draw(screen);
    toolbar_.setBounds(Rect{bounds_.x, bounds_.y + tabHeight, bounds_.w, toolbarHeight});
    toolbar_.setFocused(focused_ && toolbarActive_);
    toolbar_.draw(screen);

    if (auto* page = activePage()) {
        page->setBounds(Rect{bounds_.x, bounds_.y + tabHeight + toolbarHeight,
                             bounds_.w, pageHeight});
        page->setFocused(focused_ && !toolbarActive_);
        page->draw(screen);
        if (!page->loading() && !toolbarActive_ && !page->url().empty() && address_->value() != text::toUtf32(page->url()))
            address_->setValue(text::toUtf32(page->url()));
        if (!page->loading() && !page->title().empty()) context_.setWindowTitle(page->title());
        // Show JS errors in a popup dialog (once per new error).
        const auto& errs = page->jsErrors();
        if (errs.empty()) shownJsErrorCount_ = 0;
        if (errs.size() > shownJsErrorCount_) {
            std::string msg;
            for (std::size_t i = shownJsErrorCount_; i < errs.size(); ++i) {
                if (!msg.empty()) msg += "\n";
                msg += errs[i];
            }
            shownJsErrorCount_ = errs.size();
            context_.showMessage(text::toUtf32(msg));
        }
    }
    syncTabs();
    syncToolbar();

    status_.setBounds(Rect{bounds_.x, bounds_.y + bounds_.h - statusHeight, bounds_.w, statusHeight});
    status_.setText(U"Tab: page controls | Enter: open/edit | Ctrl+L: address | Ctrl+Enter: link in new tab",
                    activePage() && activePage()->loading() ? U"Loading" : U"");
    status_.draw(screen);
}

KeyEventResult BrowserView::handleKey(const KeyEvent& event) {
    if (event.ctrl && event.key == Key::Char) {
        switch (event.character) {
            case U'l': toolbarActive_ = true; toolbar_.focus(addressSlot_); return KeyEventResult::EnteredEdit;
            case U't': newTab(); return KeyEventResult::Consumed;
            case U'r': reload(); return KeyEventResult::Consumed;
            case U'h': goHome(); return KeyEventResult::Consumed;
            default: break;
        }
    }
    if (event.ctrl && event.key == Key::F4) { closeTab(); return KeyEventResult::Consumed; }
    if (event.ctrl && event.key == Key::Left) { goBack(); return KeyEventResult::Consumed; }
    if (event.ctrl && event.key == Key::Right) { goForward(); return KeyEventResult::Consumed; }
    if (event.ctrl && event.key == Key::Enter) { openFocusedLinkInNewTab(); return KeyEventResult::Consumed; }
    if (toolbarActive_) {
        if (event.key == Key::Escape) { toolbarActive_ = false; return KeyEventResult::Consumed; }
        return toolbar_.handleKey(event);
    }
    if (event.key == Key::Tab) {
        if (auto* page = activePage(); page && page->focusNextControl()) {
            return page->inputFocused() ? KeyEventResult::EnteredEdit : KeyEventResult::ExitedEdit;
        }
        toolbarActive_ = true;
        toolbar_.focus(addressSlot_);
        return KeyEventResult::EnteredEdit;
    }
    const KeyEventResult result = activePage() ? activePage()->handleKey(event) : KeyEventResult::Ignored;
    followPendingLink();
    return result;
}

bool BrowserView::clickAt(int x, int y) {
    if (y == tabBar_.bounds().y) {
        if (tabBar_.scrollHit(x)) return true;
        const int index = tabBar_.tabAt(x);
        if (index >= 0) { selectTab(index); return true; }
    }
    if (toolbar_.bounds().contains(x, y)) {
        toolbarActive_ = true;
        return toolbar_.clickAt(x, y);
    }
    toolbarActive_ = false;
    if (auto* page = activePage(); page && page->clickAt(x, y)) {
        followPendingLink();
        return true;
    }
    return false;
}

std::optional<std::pair<int, int>> BrowserView::cursorPosition() const {
    if (toolbarActive_) return toolbar_.cursorPosition();
    const Tab* tab = activeTab();
    return tab ? tab->page->cursorPosition() : std::nullopt;
}

bool BrowserView::wantsGuiCursor() const { return cursorPosition().has_value(); }

bool BrowserView::editable() const {
    if (toolbarActive_) return true;
    const auto* tab = activeTab();
    return tab && tab->page->inputFocused();
}

} // namespace fenrir::apps::browser
