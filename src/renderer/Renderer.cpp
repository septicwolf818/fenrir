#include "fenrir/renderer/Renderer.hpp"

#include <cstdio>
#include <string>

#include "fenrir/core/Text.hpp"

namespace fenrir {

namespace {

void appendMoveTo(std::string& out, int x, int y) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1); // ANSI: 1-indeksowane
    out += buf;
}

void appendSgr(std::string& out, const Cell& c) {
    // Linux tty: wylacznie 16 kolorow ANSI. Kolor juz JEST indeksem 0..15,
    // wiec wystarczy dac go bezposrednio do SGR — bez truecolor (38;2;...),
    // bez tablic RGB i bez szukania najblizszego koloru.
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
        // sentinelny kolor (255) — "inny niz jakikolwiek prawdziwy", tylko do porownan
        previous_.assign(cells.size(), Cell{U'\0', Color{255}, Color{255}, 0});
        prevWidth_ = w;
        prevHeight_ = h;
        fullRedraw_ = true;
    }

    std::string out;
    out.reserve(4096);

    int lastX = -1, lastY = -1;
    Cell lastStyleUsed{}; // do wykrywania czy potrzebny nowy SGR
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

            text::appendUtf8(out, cur.character == 0 ? U' ' : cur.character);
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
