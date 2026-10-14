#include "fenrir/renderer/Renderer.hpp"

#include <cstdio>
#include <string>

#include "fenrir/core/Text.hpp"

namespace fenrir {

namespace {

void appendMoveTo(std::string& out, int x, int y) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1); // ANSI: 1-indexed
    out += buf;
}

void appendSgr(std::string& out, const Cell& c) {
    // Linux tty: 16 ANSI colors only. The color already IS an index 0..15,
    // so we can pass it straight to SGR — no truecolor (38;2;...),
    // no RGB table, no nearest-color search.
    auto code = [](int ansi, bool fg) {
        if (ansi < 8) return fg ? 30 + ansi : 40 + ansi;
        return fg ? 90 + (ansi - 8) : 100 + (ansi - 8);
    };
    char buf[16];
    out += "\x1b[0m"; // reset
    if (c.style & StyleBold) out += "\x1b[1m";
    if (c.style & StyleUnderline) out += "\x1b[4m";
    if (c.style & StyleDim) out += "\x1b[2m";
    std::snprintf(buf, sizeof(buf), "\x1b[%dm", code(c.fg.index, true));
    out += buf;
    std::snprintf(buf, sizeof(buf), "\x1b[%dm", code(c.bg.index, false));
    out += buf;
}

} // namespace

Renderer::Renderer(Terminal& terminal) : terminal_(terminal) {}

void Renderer::forceFullRedraw() { fullRedraw_ = true; }

void Renderer::present(const Screen& screen) {
    const int w = screen.width();
    const int h = screen.height();
    const auto& cells = screen.cells();

    if (w != prevWidth_ || h != prevHeight_) {
        // sentinel color (255) — "different from any real one", only for comparisons
        previous_.assign(cells.size(), Cell{U'\0', Color{255}, Color{255}, 0});
        prevWidth_ = w;
        prevHeight_ = h;
        fullRedraw_ = true;
    }

    std::string out;
    out.reserve(4096);

    int lastX = -1, lastY = -1;
    Cell lastStyleUsed{}; // for detecting whether a new SGR is needed
    bool haveLastStyle = false;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * w + x;
            const Cell& cur = cells[idx];
            if (!fullRedraw_ && cur == previous_[idx]) continue;

            if (x != lastX || y != lastY) appendMoveTo(out, x, y);

            if (!haveLastStyle || cur.fg != lastStyleUsed.fg || cur.bg != lastStyleUsed.bg ||
                cur.style != lastStyleUsed.style) {
                appendSgr(out, cur);
                lastStyleUsed = cur;
                haveLastStyle = true;
            }

            // Control characters from cell data (e.g. color sequences saved
            // by Paint, newlines in file names) NEVER reach the terminal as
            // raw bytes — they would be interpreted as commands and disrupt
            // cursor positioning for everything drawn after them. Show them
            // as visible glyphs from the U+2400 block instead (ESC shows as
            // ␛ U+241B, so the Text Editor SEES raw data instead of colors).
            char32_t ch = cur.character == 0 ? U' ' : cur.character;
            if (ch < 0x20) ch = static_cast<char32_t>(0x2400 + ch);
            else if (ch == 0x7F) ch = U'␡';
            text::appendUtf8(out, ch);
            previous_[idx] = cur;

            lastX = x + 1;
            lastY = y;
        }
    }

    fullRedraw_ = false;

    if (!out.empty()) {
        out += "\x1b[0m";
        terminal_.write(out);
        terminal_.flush();
    }
}

} // namespace fenrir
