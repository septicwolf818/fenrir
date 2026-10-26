#include "fenrir/core/Clipboard.hpp"

#include "fenrir/core/FileSystem.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fenrir::clipboard {

namespace {

std::function<void(const std::string&)> g_terminalSink;

std::string clipboardPath() {
    return fs::join(fs::join(fs::homeDir(), ".config"), "fenrir/clipboard.txt");
}

} // namespace

std::string base64Encode(const std::string& data) {
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned int v = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8) |
                               static_cast<unsigned char>(data[i + 2]);
        out += table[(v >> 18) & 0x3f];
        out += table[(v >> 12) & 0x3f];
        out += table[(v >> 6) & 0x3f];
        out += table[v & 0x3f];
        i += 3;
    }
    const std::size_t rest = data.size() - i;
    if (rest == 1) {
        const unsigned int v = static_cast<unsigned char>(data[i]) << 16;
        out += table[(v >> 18) & 0x3f];
        out += table[(v >> 12) & 0x3f];
        out += "==";
    } else if (rest == 2) {
        const unsigned int v = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8);
        out += table[(v >> 18) & 0x3f];
        out += table[(v >> 12) & 0x3f];
        out += table[(v >> 6) & 0x3f];
        out += '=';
    }
    return out;
}

void setText(const std::string& utf8) {
    // Avoid rewriting the clipboard file when the content is unchanged.
    if (text() == utf8) {
        if (g_terminalSink) g_terminalSink(utf8);
        return;
    }
    std::error_code ec;
    const std::filesystem::path path(clipboardPath());
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file) file << utf8;
    if (g_terminalSink) g_terminalSink(utf8);
}

std::string text() {
    std::ifstream file(clipboardPath(), std::ios::binary);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool empty() { return text().empty(); }

void clear() {
    std::error_code ec;
    std::filesystem::remove(clipboardPath(), ec);
    if (g_terminalSink) g_terminalSink({});
}

void setTerminalSink(std::function<void(const std::string& utf8)> sink) {
    g_terminalSink = std::move(sink);
}

} // namespace fenrir::clipboard
