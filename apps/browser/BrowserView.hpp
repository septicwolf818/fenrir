#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BrowserPage.hpp"
#include "fenrir/app/AppContext.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/HorizontalLayout.hpp"
#include "fenrir/widgets/LineEdit.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/TabBar.hpp"

namespace fenrir::apps::browser {

class BrowserView final : public Widget {
public:
    explicit BrowserView(AppContext& context);

    void newTab(std::string url = {});
    void closeTab();
    void goBack();
    void goForward();
    void reload();
    void openFocusedLinkInNewTab();

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& event) override;
    bool clickAt(int x, int y) override;
    bool movePointer(int fromX, int fromY, int& toX, int& toY) override;
    void pointerMovedAt(int x, int y, int dx, int dy) override;
    bool editable() const override;
    bool dirty() const override { return false; }
    int minWidth() const override { return 32; }
    int minHeight() const override { return 6; }
    std::optional<std::pair<int, int>> cursorPosition() const override;
    bool wantsGuiCursor() const override;

private:
    struct Tab {
        std::unique_ptr<BrowserPage> page;
        std::vector<std::string> history;
        int historyIndex = -1;
        std::string shownTitle;
    };

    Tab* activeTab();
    const Tab* activeTab() const;
    BrowserPage* activePage();
    void navigateCurrent(const std::string& address, bool addHistory = true);
    void selectTab(int index);
    void syncTabs();
    void syncToolbar();
    void followPendingLink();

    AppContext& context_;
    std::vector<Tab> tabs_;
    int active_ = 0;
    TabBar tabBar_;
    HorizontalLayout toolbar_;
    Button* backButton_ = nullptr;
    Button* forwardButton_ = nullptr;
    Button* reloadButton_ = nullptr;
    Button* newTabButton_ = nullptr;
    LineEdit* address_ = nullptr;
    int addressSlot_ = -1;
    bool toolbarActive_ = false;
    StatusBar status_;
    bool titleReady_ = false;
};

} // namespace fenrir::apps::browser
