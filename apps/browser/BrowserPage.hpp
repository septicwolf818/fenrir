#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "BrowserNetwork.hpp"
#include "fenrir/widgets/Widget.hpp"
#include "litehtml.h"

namespace fenrir::apps::browser {

class BrowserContainer;

// HTML/CSS document surface. Navigation chrome and tab/history policy live in
// BrowserView; this widget owns layout, scrolling, link focus, and cell drawing.
class BrowserPage final : public Widget {
public:
    BrowserPage();
    ~BrowserPage() override;

    void navigate(std::string url);
    bool loadHtml(std::string html, std::string baseUrl);
    void poll();

    const std::string& url() const { return url_; }
    const std::string& title() const { return title_; }
    const std::string& error() const { return error_; }
    bool loading() const { return loading_; }
    int scrollRow() const { return scrollRow_; }
    const std::string& focusedLinkUrl() const;
    std::string takePendingNavigation();

    bool focusLink(int direction = 1);
    bool focusNextControl(int direction = 1);
    bool activateFocusedLink();
    bool inputFocused() const;

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& event) override;
    bool clickAt(int x, int y) override;
    bool editable() const override { return true; }
    bool wantsGuiCursor() const override;
    std::optional<std::pair<int, int>> cursorPosition() const override;

private:
    friend class BrowserContainer;
    struct Link {
        litehtml::element::ptr element;
        litehtml::position placement;
        std::string url;
    };
    struct TextField {
        litehtml::element::ptr element;
        litehtml::position placement;
        std::u32string value;
        std::u32string placeholder;
        int caret = 0;
        bool password = false;
    };
    struct FocusTarget {
        bool isTextField = false;
        std::size_t index = 0;
    };
    struct FetchState {
        std::mutex mutex;
        std::optional<Response> response;
    };

    void ensureLayout();
    void rebuildLinks();
    void updateHoveredControl();
    bool setPointerPosition(int x, int y);
    void movePageCursorBy(int dx, int dy);
    void focusControl(std::size_t index);
    void scrollIntoView(const litehtml::position& placement);
    void drawTextFields(Screen& screen);
    void setTitle(std::string title);
    void setPendingNavigation(std::string url);
    int maxScrollRow() const;

    std::unique_ptr<BrowserContainer> container_;
    litehtml::document::ptr document_;
    std::shared_ptr<FetchState> fetchState_;
    std::jthread fetchWorker_;
    std::vector<Link> links_;
    std::vector<TextField> textFields_;
    std::vector<FocusTarget> focusOrder_;
    int focusedLink_ = -1;
    int focusedTextField_ = -1;
    int focusedControl_ = -1;
    int pointerX_ = -1;
    int pointerY_ = -1;
    int scrollRow_ = 0;
    int lastLayoutWidth_ = -1;
    int lastLayoutHeight_ = -1;
    bool loading_ = false;
    std::string url_;
    std::string title_;
    std::string error_;
    std::string pendingNavigation_;
};

} // namespace fenrir::apps::browser
