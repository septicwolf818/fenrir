#include "BrowserPage.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "BrowserNetwork.hpp"
#include "fenrir/core/Screen.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"

namespace fenrir::apps::browser {
namespace {

constexpr int kCellWidthPx = 8;
constexpr int kCellHeightPx = 16;

struct PaletteColor { int r, g, b; };

const PaletteColor kPalette[] = {
    {0, 0, 0}, {170, 0, 0}, {0, 170, 0}, {170, 85, 0},
    {0, 0, 170}, {170, 0, 170}, {0, 170, 170}, {170, 170, 170},
    {85, 85, 85}, {255, 85, 85}, {85, 255, 85}, {255, 255, 85},
    {85, 85, 255}, {255, 85, 255}, {85, 255, 255}, {255, 255, 255}};

Color ansiColor(litehtml::web_color color) {
    const double alpha = color.alpha / 255.0;
    const int r = static_cast<int>(color.red * alpha);
    const int g = static_cast<int>(color.green * alpha);
    const int b = static_cast<int>(color.blue * alpha);
    int best = 0;
    long distance = std::numeric_limits<long>::max();
    for (int i = 0; i < 16; ++i) {
        const long dr = r - kPalette[i].r;
        const long dg = g - kPalette[i].g;
        const long db = b - kPalette[i].b;
        const long candidate = dr * dr + dg * dg + db * db;
        if (candidate < distance) { distance = candidate; best = i; }
    }
    return Color{static_cast<uint8_t>(best)};
}

bool wideGlyph(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11ff) || (cp >= 0x2e80 && cp <= 0xa4cf) ||
           (cp >= 0xac00 && cp <= 0xd7af) || (cp >= 0xf900 && cp <= 0xfaff) ||
           (cp >= 0xfe30 && cp <= 0xfe6f) || (cp >= 0xff00 && cp <= 0xff60) ||
           (cp >= 0x1f300 && cp <= 0x1faff);
}

int cellWidth(std::u32string_view text) {
    int width = 0;
    for (char32_t cp : text) width += wideGlyph(cp) ? 2 : 1;
    return width;
}

} // namespace

class BrowserContainer final : public litehtml::document_container {
public:
    explicit BrowserContainer(BrowserPage& page) : page_(page) {}

    void beginDraw(Screen& screen) { screen_ = &screen; }
    void endDraw() { screen_ = nullptr; }

    litehtml::uint_ptr create_font(const litehtml::font_description& description,
                                   const litehtml::document*,
                                   litehtml::font_metrics* metrics) override {
        const litehtml::uint_ptr id = nextFont_++;
        const float size = std::max(8.0f, description.size);
        fonts_[id] = Font{size, description.weight, description.decoration_line};
        if (metrics) {
            metrics->font_size = size;
            metrics->height = size;
            metrics->ascent = size * 0.8f;
            metrics->descent = size * 0.2f;
            metrics->x_height = size * 0.5f;
            metrics->ch_width = size * 0.5f;
            metrics->draw_spaces = true;
        }
        return id;
    }

    void delete_font(litehtml::uint_ptr id) override { fonts_.erase(id); }

    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr id) override {
        const auto found = fonts_.find(id);
        const float size = found == fonts_.end() ? 16.0f : found->second.size;
        return static_cast<float>(cellWidth(text::toUtf32(text ? text : ""))) *
               size * 0.5f;
    }

    void draw_text(litehtml::uint_ptr, const char* value, litehtml::uint_ptr id,
                   litehtml::web_color color, const litehtml::position& pos) override {
        if (!screen_ || !value) return;
        const auto font = fonts_.find(id);
        uint8_t style = font != fonts_.end() && font->second.weight >= 600 ? StyleBold : StyleNone;
        if (font != fonts_.end() &&
            (font->second.decoration & litehtml::text_decoration_line_underline))
            style |= StyleUnderline;
        const int x = static_cast<int>(std::floor(pos.x / kCellWidthPx));
        const int y = static_cast<int>(std::floor(pos.y / kCellHeightPx));
        putPageText(x, y, text::toUtf32(value), ansiColor(color), style);
    }

    litehtml::pixel_t pt_to_px(float points) const override { return points * (96.0f / 72.0f); }
    litehtml::pixel_t get_default_font_size() const override { return 16.0f; }
    const char* get_default_font_name() const override { return "monospace"; }

    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker& marker) override {
        if (!screen_) return;
        const int x = static_cast<int>(std::floor(marker.pos.x / kCellWidthPx));
        const int y = static_cast<int>(std::floor(marker.pos.y / kCellHeightPx));
        std::u32string glyph = U"•";
        if (marker.marker_type == litehtml::list_style_type_decimal)
            glyph = text::toUtf32(std::to_string(marker.index) + ".");
        else if (marker.marker_type == litehtml::list_style_type_square) glyph = U"▪";
        putPageText(x, y, glyph, ansiColor(marker.color), StyleNone);
    }

    void load_image(const char*, const char*, bool) override {}

    void get_image_size(const char*, const char*, litehtml::size& size) override {
        size = litehtml::size(8 * kCellWidthPx, 2 * kCellHeightPx);
    }

    void draw_image(litehtml::uint_ptr, const litehtml::background_layer& layer,
                    const std::string&, const std::string&) override {
        if (!screen_) return;
        const int x = static_cast<int>(std::floor(layer.border_box.x / kCellWidthPx));
        const int y = static_cast<int>(std::floor(layer.border_box.y / kCellHeightPx));
        putPageText(x, y, U"[image]", colors::Gray, StyleDim);
    }

    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer,
                         const litehtml::web_color& color) override {
        if (!screen_ || color.alpha == 0) return;
        auto box = layer.border_box;
        if (layer.is_root) {
            box.x = static_cast<float>(page_.bounds().x * kCellWidthPx);
            box.y = static_cast<float>(page_.bounds().y * kCellHeightPx);
            box.width = static_cast<float>(page_.bounds().w * kCellWidthPx);
            box.height = static_cast<float>(page_.bounds().h * kCellHeightPx);
        }
        fill(box, ansiColor(color));
    }

    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer& layer,
                              const litehtml::background_layer::linear_gradient& gradient) override {
        if (!gradient.color_points.empty()) draw_solid_fill(0, layer, gradient.color_points.front().color);
    }

    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer& layer,
                              const litehtml::background_layer::radial_gradient& gradient) override {
        if (!gradient.color_points.empty()) draw_solid_fill(0, layer, gradient.color_points.front().color);
    }

    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer& layer,
                             const litehtml::background_layer::conic_gradient& gradient) override {
        if (!gradient.color_points.empty()) draw_solid_fill(0, layer, gradient.color_points.front().color);
    }

    void draw_borders(litehtml::uint_ptr, const litehtml::borders& borders,
                      const litehtml::position& rect, bool) override {
        if (!screen_ || !borders.is_visible()) return;
        const int left = static_cast<int>(std::floor(rect.x / kCellWidthPx));
        const int top = static_cast<int>(std::floor(rect.y / kCellHeightPx));
        const int right = static_cast<int>(std::ceil(rect.right() / kCellWidthPx)) - 1;
        const int bottom = static_cast<int>(std::ceil(rect.bottom() / kCellHeightPx)) - 1;
        const auto put = [&](int x, int y, char32_t glyph, const litehtml::border& border) {
            const Rect page = page_.bounds();
            if (border.width <= 0 || !page.contains(x, y)) return;
            screen_->set(x, y, Cell{glyph, ansiColor(border.color), colors::Black, StyleNone});
        };
        for (int x = left; x <= right; ++x) {
            put(x, top, U'─', borders.top);
            put(x, bottom, U'─', borders.bottom);
        }
        for (int y = top; y <= bottom; ++y) {
            put(left, y, U'│', borders.left);
            put(right, y, U'│', borders.right);
        }
        put(left, top, U'┌', borders.left.width > 0 ? borders.left : borders.top);
        put(right, top, U'┐', borders.right.width > 0 ? borders.right : borders.top);
        put(left, bottom, U'└', borders.left.width > 0 ? borders.left : borders.bottom);
        put(right, bottom, U'┘', borders.right.width > 0 ? borders.right : borders.bottom);
    }

    void set_caption(const char* caption) override { page_.setTitle(caption ? caption : ""); }
    void set_base_url(const char* baseUrl) override {
        if (baseUrl) baseUrl_ = baseUrl;
    }
    void link(const std::shared_ptr<litehtml::document>&,
              const litehtml::element::ptr&) override {}
    void on_anchor_click(const char* url, const litehtml::element::ptr&) override {
        if (url) page_.setPendingNavigation(browser::resolveUrl(baseUrl_, url));
    }
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}

    void transform_text(litehtml::string& value, litehtml::text_transform transform) override {
        if (transform == litehtml::text_transform_none) return;
        for (char& ch : value) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (transform == litehtml::text_transform_uppercase) ch = static_cast<char>(std::toupper(c));
            else if (transform == litehtml::text_transform_lowercase) ch = static_cast<char>(std::tolower(c));
        }
    }

    void import_css(litehtml::string& value, const litehtml::string& url,
                    litehtml::string& baseUrl) override {
        if (cssRequests_ >= 8) return;
        const std::string resolved = browser::resolveUrl(baseUrl.empty() ? baseUrl_ : baseUrl, url);
        if (resolved.empty() || !loadedCss_.insert(resolved).second) return;
        ++cssRequests_;
        Response response = browser::fetch(resolved, 512 * 1024, 2500);
        if (response.error.empty()) {
            value = std::move(response.body);
            if (!response.finalUrl.empty()) baseUrl = response.finalUrl;
        }
    }

    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}

    void get_viewport(litehtml::position& viewport) const override {
        viewport = litehtml::position(0, 0, page_.bounds().w * kCellWidthPx,
                                      page_.bounds().h * kCellHeightPx);
    }

    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                           const std::shared_ptr<litehtml::document>&) override {
        return {};
    }

    void get_media_features(litehtml::media_features& media) const override {
        media.type = litehtml::media_type_screen;
        media.width = media.device_width = static_cast<float>(page_.bounds().w * kCellWidthPx);
        media.height = media.device_height = static_cast<float>(page_.bounds().h * kCellHeightPx);
        media.color = 4;
        media.color_index = 16;
        media.resolution = 96;
    }

    void get_language(litehtml::string& language, litehtml::string& culture) const override {
        language = "en";
        culture = "US";
    }

private:
    struct Font { float size; int weight; int decoration; };

    void fill(const litehtml::position& box, Color bg) {
        const int left = static_cast<int>(std::floor(box.x / kCellWidthPx));
        const int top = static_cast<int>(std::floor(box.y / kCellHeightPx));
        const int right = static_cast<int>(std::ceil(box.right() / kCellWidthPx));
        const int bottom = static_cast<int>(std::ceil(box.bottom() / kCellHeightPx));
        const Rect page = page_.bounds();
        const int x0 = std::max(left, page.x);
        const int y0 = std::max(top, page.y);
        const int x1 = std::min(right, page.x + page.w);
        const int y1 = std::min(bottom, page.y + page.h);
        if (x1 > x0 && y1 > y0)
            screen_->fillRect(Rect{x0, y0, x1 - x0, y1 - y0}, Cell{U' ', colors::White, bg, 0});
    }

    void putPageText(int x, int y, std::u32string_view value, Color fg, uint8_t style) {
        if (!screen_) return;
        const Rect page = page_.bounds();
        if (y < page.y || y >= page.y + page.h) return;
        int column = x;
        for (char32_t cp : value) {
            const int width = wideGlyph(cp) ? 2 : 1;
            if (column >= page.x + page.w) break;
            if (column >= page.x) {
                const Color bg = screen_->at(column, y).bg;
                screen_->set(column, y, Cell{cp, fg, bg, style});
                for (int i = 1; i < width && column + i < page.x + page.w; ++i)
                    screen_->set(column + i, y, Cell{U' ', fg, bg, style});
            }
            column += width;
        }
    }

    BrowserPage& page_;
    Screen* screen_ = nullptr;
    litehtml::uint_ptr nextFont_ = 1;
    std::map<litehtml::uint_ptr, Font> fonts_;
    std::set<std::string> loadedCss_;
    int cssRequests_ = 0;
    std::string baseUrl_;
};

BrowserPage::BrowserPage() : container_(std::make_unique<BrowserContainer>(*this)) {}
BrowserPage::~BrowserPage() {
    if (fetchWorker_.joinable()) fetchWorker_.request_stop();
}

void BrowserPage::navigate(std::string url) {
    if (fetchWorker_.joinable()) {
        fetchWorker_.request_stop();
        fetchWorker_.join();
    }
    fetchState_.reset();
    document_.reset();
    links_.clear();
    error_.clear();
    pendingNavigation_.clear();
    focusedLink_ = -1;
    scrollRow_ = 0;
    lastLayoutWidth_ = lastLayoutHeight_ = -1;
    url = browser::normalizeUrl(url);
    if (url.empty()) {
        url_.clear();
        title_ = "New Tab";
        error_ = "Enter an HTTP or HTTPS address.";
        loading_ = false;
        return;
    }
    url_ = std::move(url);
    title_ = url_;
    error_.clear();
    pendingNavigation_.clear();
    focusedLink_ = -1;
    scrollRow_ = 0;
    lastLayoutWidth_ = lastLayoutHeight_ = -1;
    document_.reset();
    loading_ = true;
    fetchState_ = std::make_shared<FetchState>();
    const auto result = fetchState_;
    const std::string requestUrl = url_;
    fetchWorker_ = std::jthread([result, requestUrl](std::stop_token stop) {
        Response response = browser::fetch(requestUrl, 4 * 1024 * 1024, 10000, stop);
        std::lock_guard lock(result->mutex);
        result->response = std::move(response);
    });
}

bool BrowserPage::loadHtml(std::string html, std::string baseUrl) {
    if (fetchWorker_.joinable()) {
        fetchWorker_.request_stop();
        fetchWorker_.join();
    }
    fetchState_.reset();
    loading_ = false;
    error_.clear();
    url_ = std::move(baseUrl);
    title_ = url_.empty() ? "New Tab" : url_;
    pendingNavigation_.clear();
    focusedLink_ = -1;
    scrollRow_ = 0;
    lastLayoutWidth_ = lastLayoutHeight_ = -1;
    container_ = std::make_unique<BrowserContainer>(*this);
    container_->set_base_url(url_.c_str());
    document_ = litehtml::document::createFromString(html, container_.get());
    if (!document_) {
        error_ = "Could not parse HTML.";
        return false;
    }
    ensureLayout();
    rebuildLinks();
    return true;
}

void BrowserPage::poll() {
    if (!loading_ || !fetchState_) return;
    std::optional<Response> result;
    {
        std::lock_guard lock(fetchState_->mutex);
        if (fetchState_->response) result = std::move(fetchState_->response);
    }
    if (!result) return;
    loading_ = false;
    fetchState_.reset();
    if (!result->error.empty()) {
        error_ = result->error;
        return;
    }

    url_ = result->finalUrl.empty() ? result->requestedUrl : result->finalUrl;
    std::string type = result->contentType;
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type.find("text/html") == std::string::npos &&
        type.find("application/xhtml+xml") == std::string::npos) {
        error_ = "This prototype currently displays HTML pages only.";
        return;
    }
    (void)loadHtml(std::move(result->body), url_);
}

void BrowserPage::ensureLayout() {
    if (!document_ || bounds_.w <= 0 || bounds_.h <= 0) return;
    if (lastLayoutWidth_ == bounds_.w && lastLayoutHeight_ == bounds_.h) return;
    lastLayoutWidth_ = bounds_.w;
    lastLayoutHeight_ = bounds_.h;
    (void)document_->media_changed();
    (void)document_->render(static_cast<float>(bounds_.w * kCellWidthPx));
    scrollRow_ = std::clamp(scrollRow_, 0, maxScrollRow());
    rebuildLinks();
}

void BrowserPage::rebuildLinks() {
    links_.clear();
    if (!document_ || !document_->root()) { focusedLink_ = -1; return; }
    auto include = [](litehtml::position& bounds, const litehtml::position& item, bool& hasBounds) {
        if (item.width <= 0 || item.height <= 0) return;
        if (!hasBounds) { bounds = item; hasBounds = true; return; }
        const float right = std::max(bounds.right(), item.right());
        const float bottom = std::max(bounds.bottom(), item.bottom());
        bounds.x = std::min(bounds.x, item.x);
        bounds.y = std::min(bounds.y, item.y);
        bounds.width = right - bounds.x;
        bounds.height = bottom - bounds.y;
    };
    std::function<void(const litehtml::element::ptr&)> visit = [&](const litehtml::element::ptr& element) {
        if (std::string(element->get_tagName()) == "a") {
            const char* href = element->get_attr("href");
            if (href && *href) {
                litehtml::position placement;
                litehtml::position clickPlacement;
                bool hasPlacement = false;
                std::function<void(const litehtml::element::ptr&)> includeText =
                    [&](const litehtml::element::ptr& child) {
                        const auto childPlacement = child->get_placement();
                        if (!hasPlacement && childPlacement.width > 0 && childPlacement.height > 0)
                            clickPlacement = childPlacement;
                        include(placement, childPlacement, hasPlacement);
                        for (const auto& descendant : child->children()) includeText(descendant);
                    };
                for (const auto& child : element->children()) includeText(child);
                include(placement, element->get_placement(), hasPlacement);
                if (hasPlacement)
                    links_.push_back(Link{element, placement, clickPlacement,
                                          browser::resolveUrl(url_, href)});
            }
        }
        for (const auto& child : element->children()) visit(child);
    };
    visit(document_->root());
    if (links_.empty()) focusedLink_ = -1;
    else if (focusedLink_ >= static_cast<int>(links_.size()))
        focusedLink_ = static_cast<int>(links_.size()) - 1;
}

void BrowserPage::setTitle(std::string title) {
    if (!title.empty()) title_ = std::move(title);
}

void BrowserPage::setPendingNavigation(std::string url) {
    if (!url.empty()) pendingNavigation_ = std::move(url);
}

const std::string& BrowserPage::focusedLinkUrl() const {
    static const std::string empty;
    return focusedLink_ >= 0 && static_cast<std::size_t>(focusedLink_) < links_.size()
               ? links_[static_cast<std::size_t>(focusedLink_)].url
               : empty;
}

std::string BrowserPage::takePendingNavigation() {
    return std::exchange(pendingNavigation_, {});
}

int BrowserPage::maxScrollRow() const {
    if (!document_) return 0;
    const int rows = static_cast<int>(std::ceil(document_->height() / kCellHeightPx));
    return std::max(0, rows - bounds_.h);
}

bool BrowserPage::focusLink(int direction) {
    ensureLayout();
    if (links_.empty()) return false;
    if (focusedLink_ < 0) focusedLink_ = direction >= 0 ? 0 : static_cast<int>(links_.size()) - 1;
    else focusedLink_ = (focusedLink_ + direction + static_cast<int>(links_.size())) %
                        static_cast<int>(links_.size());
    const auto& placement = links_[static_cast<std::size_t>(focusedLink_)].placement;
    const int top = static_cast<int>(std::floor(placement.y / kCellHeightPx));
    const int bottom = static_cast<int>(std::ceil(placement.bottom() / kCellHeightPx));
    if (top < scrollRow_) scrollRow_ = top;
    else if (bottom > scrollRow_ + bounds_.h) scrollRow_ = bottom - bounds_.h;
    scrollRow_ = std::clamp(scrollRow_, 0, maxScrollRow());
    if (document_) {
        litehtml::position::vector redraw;
        const auto& p = links_[static_cast<std::size_t>(focusedLink_)].placement;
        const auto x = p.x + p.width / 2;
        const auto y = p.y + p.height / 2;
        (void)document_->on_mouse_over(x, y, x, y, redraw);
    }
    return true;
}

bool BrowserPage::activateFocusedLink() {
    if (!document_ || focusedLink_ < 0 || static_cast<std::size_t>(focusedLink_) >= links_.size())
        return false;
    const auto& link = links_[static_cast<std::size_t>(focusedLink_)];
    const auto& p = link.clickPlacement.width > 0 ? link.clickPlacement : link.placement;
    const auto x = p.x + p.width / 2;
    const auto y = p.y + p.height / 2;
    litehtml::position::vector redraw;
    (void)document_->on_lbutton_down(x, y, x, y, redraw);
    return document_->on_lbutton_up(x, y, x, y, redraw);
}

void BrowserPage::draw(Screen& screen) {
    poll();
    if (bounds_.w <= 0 || bounds_.h <= 0) return;
    const auto& themeColors = theme::active;
    screen.fillRect(bounds_, Cell{U' ', themeColors.editorText, colors::Black, 0});
    if (loading_) {
        screen.putText(bounds_.x, bounds_.y, U"Loading page...", themeColors.windowText, colors::Black);
        return;
    }
    if (!error_.empty()) {
        screen.putText(bounds_.x, bounds_.y, text::clip(text::toUtf32(error_), bounds_.w),
                       themeColors.errorText, colors::Black, StyleBold);
        return;
    }
    if (!document_) {
        screen.putText(bounds_.x, bounds_.y, U"Enter an HTTP or HTTPS address to browse.",
                       themeColors.windowText, colors::Black);
        return;
    }
    ensureLayout();
    container_->beginDraw(screen);
    const float scrollPx = static_cast<float>(scrollRow_ * kCellHeightPx);
    const litehtml::position clip{0, scrollPx, static_cast<float>(bounds_.w * kCellWidthPx),
                                  static_cast<float>(bounds_.h * kCellHeightPx)};
    document_->draw(0, static_cast<float>(bounds_.x * kCellWidthPx),
                    static_cast<float>(bounds_.y * kCellHeightPx) - scrollPx, &clip);
    container_->endDraw();
}

KeyEventResult BrowserPage::handleKey(const KeyEvent& event) {
    if (event.ctrl || event.alt) return KeyEventResult::Ignored;
    switch (event.key) {
        case Key::Tab: return focusLink(1) ? KeyEventResult::Consumed : KeyEventResult::Ignored;
        case Key::Enter: return activateFocusedLink() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        case Key::Up: scrollRow_ = std::max(0, scrollRow_ - 1); return KeyEventResult::Consumed;
        case Key::Down: scrollRow_ = std::min(maxScrollRow(), scrollRow_ + 1); return KeyEventResult::Consumed;
        case Key::PageUp: scrollRow_ = std::max(0, scrollRow_ - std::max(1, bounds_.h - 1)); return KeyEventResult::Consumed;
        case Key::PageDown: scrollRow_ = std::min(maxScrollRow(), scrollRow_ + std::max(1, bounds_.h - 1)); return KeyEventResult::Consumed;
        case Key::Home: scrollRow_ = 0; return KeyEventResult::Consumed;
        case Key::End: scrollRow_ = maxScrollRow(); return KeyEventResult::Consumed;
        default: return KeyEventResult::Ignored;
    }
}

bool BrowserPage::clickAt(int x, int y) {
    if (!document_ || !bounds_.contains(x, y)) return false;
    const float px = static_cast<float>((x - bounds_.x) * kCellWidthPx);
    const float py = static_cast<float>((y - bounds_.y + scrollRow_) * kCellHeightPx);
    litehtml::position::vector redraw;
    (void)document_->on_mouse_over(px, py, px, py, redraw);
    (void)document_->on_lbutton_down(px, py, px, py, redraw);
    (void)document_->on_lbutton_up(px, py, px, py, redraw);
    return true;
}

bool BrowserPage::wantsGuiCursor() const { return focusedLink_ >= 0; }

std::optional<std::pair<int, int>> BrowserPage::cursorPosition() const {
    if (focusedLink_ < 0 || static_cast<std::size_t>(focusedLink_) >= links_.size()) return std::nullopt;
    const auto& p = links_[static_cast<std::size_t>(focusedLink_)].placement;
    const int x = bounds_.x + static_cast<int>((p.x + p.width / 2) / kCellWidthPx);
    const int y = bounds_.y + static_cast<int>((p.y + p.height / 2) / kCellHeightPx) - scrollRow_;
    return std::make_pair(std::clamp(x, bounds_.x, bounds_.x + std::max(0, bounds_.w - 1)),
                          std::clamp(y, bounds_.y, bounds_.y + std::max(0, bounds_.h - 1)));
}

} // namespace fenrir::apps::browser
