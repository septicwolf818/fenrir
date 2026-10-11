#include "fenrir/renderer/Renderer.hpp"

#include <cstdio>
#include <string>

namespace fenrir {

namespace {

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void appendMoveTo(std::string& out, int x, int y) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1); // ANSI: 1-indeksowane
    out += buf;
}

void appendSgr(std::string& out, const Cell& c) {
    // Linux tty: only 16 ANSI basic colors (fallback for fbcon/serial).
    static const int kAnsi[16][3] = {
        {0,0,0},       {128,0,0},   {0,128,0},  {128,128,0},
        {0,0,128},     {128,0,128}, {0,128,128},{192,192,192},
        {128,128,128}, {255,0,0},   {0,255,0},  {255,255,0},
        {0,0,255},     {255,0,255}, {0,255,255},{255,255,255},
    };
    auto nearest = [](const Color& col) {
        int best = 0, bestD = 1 << 30;
        for (int i = 0; i < 16; ++i) {
            int dr = col.r - kAnsi[i][0], dg = col.g - kAnsi[i][1], db = col.b - kAnsi[i][2];
            int d = dr*dr + dg*dg + db*db;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    };
    auto code = [](int ansi, bool fg) {
        if (ansi < 8) return fg ? 30 + ansi : 40 + ansi;
        return fg ? 90 + (ansi - 8) : 100 + (ansi - 8);
    };
    char buf[16];
    out += "\x1b[0m"; // reset
    if (c.style & StyleBold) out += "\x1b[1m";
    if (c.style & StyleUnderline) out += "\x1b[4m";
    std::snprintf(buf, sizeof(buf), "\x1b[%dm", code(nearest(c.fg), true));
    out += buf;
    std::snprintf(buf, sizeof(buf), "\x1b[%dm", code(nearest(c.bg), false));
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
        previous_.assign(cells.size(), Cell{U'\0', Color{255, 255, 255}, Color{255, 255, 255}, 0});
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

            appendUtf8(out, cur.character == 0 ? U' ' : cur.character);
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
