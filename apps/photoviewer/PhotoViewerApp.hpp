#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/StatusBar.hpp"

#include "Image.hpp"

namespace fenrir::apps {

class PhotoView final : public Widget {
public:
    explicit PhotoView(AppContext& ctx) : ctx_(ctx) {}

    void setImagePath(const std::string& path) {
        std::string error;
        if (image_.load(path, error)) {
            path_ = path;
            error_.clear();
            fitPending_ = true;
            status_.setMessage(text::toUtf32("Loaded: " + fs::nameOf(path)));
        } else {
            path_ = path;
            error_ = std::move(error);
            status_.setMessage(text::toUtf32(error_));
        }
    }

    void openImage() {
        const std::string startDir = path_.empty() ? fs::homeDir() : fs::parent(path_);
        const auto token = lifetimeToken();
        ctx_.openFile(startDir, [this, token](const std::string& path) {
            if (token.lock()) setImagePath(path);
        });
    }

    void zoomIn() { setZoom(std::min(400, zoomPercent_ + std::max(10, zoomPercent_ / 4))); }
    void zoomOut() { setZoom(std::max(1, zoomPercent_ - std::max(10, zoomPercent_ / 4))); }

    void fitToView() {
        if (image_.width() <= 0 || image_.height() <= 0 || bounds_.w <= 0 || bounds_.h <= 1) return;
        const int viewH = bounds_.h - 1;
        const double fitW = static_cast<double>(bounds_.w) * 100.0 / image_.width();
        const double fitH = static_cast<double>(viewH) * 200.0 / image_.height();
        setZoom(static_cast<int>(std::floor(std::min(fitW, fitH))));
        panX_ = panY_ = 0.0;
    }

    void draw(Screen& screen) override {
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        const auto& t = theme::active;
        const int viewH = std::max(0, bounds_.h - 1);
        screen.fillRect(Rect{bounds_.x, bounds_.y, bounds_.w, viewH},
                        Cell{U' ', colors::White, colors::Black, 0});

        if (fitPending_ && image_.width() > 0) {
            fitToView();
            fitPending_ = false;
        }

        if (!error_.empty()) {
            screen.putText(bounds_.x, bounds_.y, text::clip(text::toUtf32(error_), bounds_.w),
                           t.errorText, colors::Black, StyleBold);
        } else if (image_.width() <= 0) {
            screen.putText(bounds_.x, bounds_.y, U"Open an image with Ctrl+O or from File Manager.",
                           t.windowText, colors::Black);
        } else {
            drawImage(screen, viewH);
        }

        status_.setBounds(Rect{bounds_.x, bounds_.y + viewH, bounds_.w, 1});
        status_.setText(U"Arrows: pan | +/-: zoom | F: fit | 0: actual size",
                        text::toUtf32(std::to_string(image_.width()) + "x" +
                                      std::to_string(image_.height()) + " " +
                                      std::to_string(zoomPercent_) + "%"));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.ctrl || ev.alt) return KeyEventResult::Ignored;
        switch (ev.key) {
            case Key::Left:  pan(-1, 0); return KeyEventResult::Consumed;
            case Key::Right: pan(1, 0); return KeyEventResult::Consumed;
            case Key::Up:    pan(0, -1); return KeyEventResult::Consumed;
            case Key::Down:  pan(0, 1); return KeyEventResult::Consumed;
            case Key::Home:  panX_ = panY_ = 0.0; return KeyEventResult::Consumed;
            case Key::PageUp: zoomIn(); return KeyEventResult::Consumed;
            case Key::PageDown: zoomOut(); return KeyEventResult::Consumed;
            case Key::Char:
                if (ev.character == U'+' || ev.character == U'=') { zoomIn(); return KeyEventResult::Consumed; }
                if (ev.character == U'-') { zoomOut(); return KeyEventResult::Consumed; }
                if (ev.character == U'0') { setZoom(100); return KeyEventResult::Consumed; }
                if (ev.character == U'f' || ev.character == U'F') { fitToView(); return KeyEventResult::Consumed; }
                break;
            default: break;
        }
        return KeyEventResult::Ignored;
    }

    bool editable() const override { return true; }
    bool dirty() const override { return false; }
    bool wantsGuiCursor() const override { return false; }
    int minWidth() const override { return 24; }
    int minHeight() const override { return 4; }

private:
    static Color nearestAnsi(const RgbPixel& p) {
        static constexpr int palette[16][3] = {
            {0, 0, 0}, {205, 0, 0}, {0, 205, 0}, {205, 205, 0},
            {0, 0, 238}, {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
            {127, 127, 127}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
            {92, 92, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
        int best = 0;
        long bestDistance = std::numeric_limits<long>::max();
        for (int i = 0; i < 16; ++i) {
            const long dr = static_cast<int>(p.r) - palette[i][0];
            const long dg = static_cast<int>(p.g) - palette[i][1];
            const long db = static_cast<int>(p.b) - palette[i][2];
            // Human vision is more sensitive to green than red or blue.
            const long distance = 3 * dr * dr + 6 * dg * dg + 2 * db * db;
            if (distance < bestDistance) { bestDistance = distance; best = i; }
        }
        return Color{static_cast<uint8_t>(best)};
    }

    RgbPixel averageRegion(double x0, double y0, double x1, double y1) const {
        const int left = std::clamp(static_cast<int>(std::floor(x0)), 0, image_.width() - 1);
        const int top = std::clamp(static_cast<int>(std::floor(y0)), 0, image_.height() - 1);
        const int right = std::clamp(std::max(left + 1, static_cast<int>(std::ceil(x1))),
                                     left + 1, image_.width());
        const int bottom = std::clamp(std::max(top + 1, static_cast<int>(std::ceil(y1))),
                                      top + 1, image_.height());
        uint64_t red = 0, green = 0, blue = 0, count = 0;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const RgbPixel& p = image_.at(x, y);
                red += p.r; green += p.g; blue += p.b; ++count;
            }
        }
        return {static_cast<uint8_t>(red / count), static_cast<uint8_t>(green / count),
                static_cast<uint8_t>(blue / count)};
    }

    void drawImage(Screen& screen, int viewH) {
        static constexpr char32_t ramp[] = U" .:-=+*#%@";
        const double sourceW = 100.0 / zoomPercent_;
        const double sourceH = 200.0 / zoomPercent_;
        const double maxX = std::max(0.0, image_.width() - bounds_.w * sourceW);
        const double maxY = std::max(0.0, image_.height() - viewH * sourceH);
        panX_ = std::clamp(panX_, 0.0, maxX);
        panY_ = std::clamp(panY_, 0.0, maxY);

        for (int y = 0; y < viewH; ++y) {
            const double y0 = panY_ + y * sourceH;
            if (y0 >= image_.height()) break;
            for (int x = 0; x < bounds_.w; ++x) {
                const double x0 = panX_ + x * sourceW;
                if (x0 >= image_.width()) break;
                const double yMid = y0 + sourceH / 2.0;
                const RgbPixel top = averageRegion(x0, y0, x0 + sourceW, yMid);
                const RgbPixel bottom = averageRegion(x0, yMid, x0 + sourceW, y0 + sourceH);
                const RgbPixel color{
                    static_cast<uint8_t>((static_cast<unsigned>(top.r) + bottom.r) / 2),
                    static_cast<uint8_t>((static_cast<unsigned>(top.g) + bottom.g) / 2),
                    static_cast<uint8_t>((static_cast<unsigned>(top.b) + bottom.b) / 2)};
                const int luminance = (2126 * color.r + 7152 * color.g + 722 * color.b) / 10000;
                const std::size_t shade = static_cast<std::size_t>(luminance) * 10 / 256;
                screen.set(bounds_.x + x, bounds_.y + y,
                           Cell{ramp[shade], nearestAnsi(top), nearestAnsi(bottom), StyleNone});
            }
        }
    }

    void setZoom(int zoom) {
        zoomPercent_ = std::clamp(zoom, 1, 400);
        status_.setMessage(text::toUtf32("Zoom: " + std::to_string(zoomPercent_) + "%"));
    }

    void pan(int dx, int dy) {
        panX_ += dx * (100.0 / zoomPercent_);
        panY_ += dy * (200.0 / zoomPercent_);
    }

    AppContext& ctx_;
    Image image_;
    std::string path_;
    std::string error_;
    StatusBar status_;
    int zoomPercent_ = 100;
    double panX_ = 0.0;
    double panY_ = 0.0;
    bool fitPending_ = false;
};

class PhotoViewerApp final : public App {
public:
    std::string name() const override { return "Photo Viewer"; }
    std::string id() const override { return "photo-viewer"; }
    std::vector<std::string> fileExtensions() const override {
        return {"bmp", "gif", "hdr", "jpeg", "jpg", "pbm", "pic", "pgm", "png",
                "pnm", "ppm", "psd", "tga"};
    }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<PhotoView>(ctx);
        PhotoView* raw = view.get();
        view->setMenuTabs({
            {U"File", std::vector<MenuItem>{
                {U"Open image... (Ctrl+O)", [raw] { raw->openImage(); }},
            }},
            {U"View", std::vector<MenuItem>{
                {U"Zoom in (+)", [raw] { raw->zoomIn(); }},
                {U"Zoom out (-)", [raw] { raw->zoomOut(); }},
                {U"Fit to window (F)", [raw] { raw->fitToView(); }},
            }},
        });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openImage(); });
        return view;
    }

    std::unique_ptr<Widget> createFileView(AppContext& ctx, const std::string& path) override {
        auto view = std::make_unique<PhotoView>(ctx);
        view->setImagePath(path);
        return view;
    }

    Rect preferredBounds(const Rect& area) const override {
        const int w = std::min(72, std::max(24, area.w - 6));
        const int h = std::min(22, std::max(6, area.h - 3));
        return Rect{area.x + std::max(0, (area.w - w) / 2),
                    area.y + std::max(0, (area.h - h) / 2), w, h};
    }
};

} // namespace fenrir::apps
