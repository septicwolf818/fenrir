#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include "../../third_party/stb_image.h"

namespace fenrir::apps {

struct RgbPixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

// Decode common raster formats into a bounded RGB buffer for terminal rendering.
class Image {
public:
    bool load(const std::string& path, std::string& error) {
        width_ = height_ = 0;
        pixels_.clear();
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) { error = "Cannot read file."; return false; }

        std::ifstream file(path, std::ios::binary);
        if (!file) { error = "Cannot open file."; return false; }
        std::vector<uint8_t> bytes;
        try {
            bytes.resize(static_cast<std::size_t>(size));
        } catch (...) {
            error = "Image file is too large to load.";
            return false;
        }
        if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file && !bytes.empty()) { error = "Could not read the complete file."; return false; }
        return loadFromBytes(bytes, error);
    }

    bool loadFromBytes(const std::vector<uint8_t>& bytes, std::string& error) {
        width_ = height_ = 0;
        pixels_.clear();
        bool decoded = false;
        try {
            if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M')
                decoded = decodeBmp(bytes, error);
            else if (!bytes.empty() && bytes[0] == 'P')
                decoded = decodePnm(bytes, error);
            else
                error = "Unsupported image format.";
            if (!decoded) {
                width_ = height_ = 0;
                pixels_.clear();
                decoded = decodeCommon(bytes, error);
            }
        } catch (const std::bad_alloc&) {
            width_ = height_ = 0;
            pixels_.clear();
            error = "Image is too large to load into memory.";
            decoded = false;
        } catch (const std::exception& e) {
            width_ = height_ = 0;
            pixels_.clear();
            error = e.what();
            decoded = false;
        }
        if (!decoded) {
            width_ = height_ = 0;
            pixels_.clear();
        }
        return decoded;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    const RgbPixel& at(int x, int y) const {
        static const RgbPixel black{};
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return black;
        return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(x)];
    }

private:
    static bool validDimensions(uint64_t w, uint64_t h) {
        if (w == 0 || h == 0) return false;
        if (w > static_cast<uint64_t>(std::numeric_limits<int>::max())) return false;
        if (h > static_cast<uint64_t>(std::numeric_limits<int>::max())) return false;
        // Guard against size_t overflow when allocating the pixel buffer.
        if (w > std::numeric_limits<std::size_t>::max() / h) return false;
        return true;
    }

    bool decodeCommon(const std::vector<uint8_t>& bytes, std::string& error) {
        if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            error = "Image data is too large or empty.";
            return false;
        }

        int w = 0, h = 0, sourceChannels = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                   &w, &h, &sourceChannels)) {
            error = "Unsupported or damaged image.";
            return false;
        }
        if (!validDimensions(static_cast<uint64_t>(w), static_cast<uint64_t>(h))) {
            error = "Image dimensions are invalid.";
            return false;
        }

        stbi_uc* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                              &w, &h, &sourceChannels, STBI_rgb_alpha);
        if (!rgba) {
            const char* reason = stbi_failure_reason();
            error = reason ? reason : "Could not decode image.";
            return false;
        }
        if (!validDimensions(static_cast<uint64_t>(w), static_cast<uint64_t>(h))) {
            stbi_image_free(rgba);
            error = "Image dimensions are invalid.";
            return false;
        }

        width_ = w;
        height_ = h;
        pixels_.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            const unsigned alpha = rgba[i * 4 + 3];
            pixels_[i] = {
                static_cast<uint8_t>((rgba[i * 4] * alpha) / 255),
                static_cast<uint8_t>((rgba[i * 4 + 1] * alpha) / 255),
                static_cast<uint8_t>((rgba[i * 4 + 2] * alpha) / 255)};
        }
        stbi_image_free(rgba);
        error.clear();
        return true;
    }

    static bool parsePositive(const std::string& token, int& value) {
        if (token.empty()) return false;
        uint64_t n = 0;
        for (unsigned char c : token) {
            if (!std::isdigit(c)) return false;
            const unsigned digit = static_cast<unsigned>(c - '0');
            if (n > (static_cast<uint64_t>(std::numeric_limits<int>::max()) - digit) / 10) return false;
            n = n * 10 + digit;
        }
        value = static_cast<int>(n);
        return true;
    }

    static uint16_t le16(const std::vector<uint8_t>& b, std::size_t o) {
        return static_cast<uint16_t>(b[o]) | (static_cast<uint16_t>(b[o + 1]) << 8);
    }

    static uint32_t le32(const std::vector<uint8_t>& b, std::size_t o) {
        return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
               (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
    }

    bool decodeBmp(const std::vector<uint8_t>& b, std::string& error) {
        if (b.size() < 54) { error = "Invalid BMP header."; return false; }
        const uint32_t pixelOffset = le32(b, 10);
        const uint32_t dibSize = le32(b, 14);
        if (dibSize < 40 || static_cast<uint64_t>(14) + dibSize > b.size()) {
            error = "Unsupported BMP header.";
            return false;
        }

        const int64_t width = static_cast<int32_t>(le32(b, 18));
        const int64_t signedHeight = static_cast<int32_t>(le32(b, 22));
        const uint16_t planes = le16(b, 26);
        const uint16_t bitsPerPixel = le16(b, 28);
        const uint32_t compression = le32(b, 30);
        const uint64_t height = signedHeight < 0 ? static_cast<uint64_t>(-signedHeight)
                                                 : static_cast<uint64_t>(signedHeight);
        if (width <= 0 || signedHeight == 0 || planes != 1 ||
            (bitsPerPixel != 24 && bitsPerPixel != 32) || compression != 0 ||
            !validDimensions(static_cast<uint64_t>(width), height)) {
            error = "BMP must be uncompressed 24-bit or 32-bit RGB.";
            return false;
        }

        const uint64_t rowBytes = (static_cast<uint64_t>(width) * bitsPerPixel + 31) / 32 * 4;
        if (rowBytes == 0 || height > std::numeric_limits<std::size_t>::max() / rowBytes) {
            error = "BMP pixel data is incomplete.";
            return false;
        }
        if (pixelOffset > b.size() || rowBytes * height > b.size() - pixelOffset) {
            error = "BMP pixel data is incomplete.";
            return false;
        }

        width_ = static_cast<int>(width);
        height_ = static_cast<int>(height);
        pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
        const std::size_t bytesPerPixel = bitsPerPixel / 8;
        for (int y = 0; y < height_; ++y) {
            const int sourceY = signedHeight > 0 ? height_ - y - 1 : y;
            const std::size_t row = static_cast<std::size_t>(pixelOffset) +
                                    static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(rowBytes);
            for (int x = 0; x < width_; ++x) {
                const std::size_t p = row + static_cast<std::size_t>(x) * bytesPerPixel;
                pixels_[static_cast<std::size_t>(y) * width_ + x] = {b[p + 2], b[p + 1], b[p]};
            }
        }
        return true;
    }

    class TokenReader {
    public:
        explicit TokenReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

        std::string next() {
            skipSpaceAndComments();
            const std::size_t begin = pos_;
            while (pos_ < bytes_.size() && !std::isspace(bytes_[pos_]) && bytes_[pos_] != '#') ++pos_;
            return std::string(reinterpret_cast<const char*>(bytes_.data() + begin), pos_ - begin);
        }

        bool consumeBinarySeparator() {
            if (pos_ >= bytes_.size() || !std::isspace(bytes_[pos_])) return false;
            if (bytes_[pos_++] == '\r' && pos_ < bytes_.size() && bytes_[pos_] == '\n') ++pos_;
            return true;
        }

        bool readByte(uint8_t& value) {
            if (pos_ >= bytes_.size()) return false;
            value = bytes_[pos_++];
            return true;
        }

        bool readWord(uint16_t& value) {
            if (pos_ + 1 >= bytes_.size()) return false;
            value = static_cast<uint16_t>((bytes_[pos_] << 8) | bytes_[pos_ + 1]);
            pos_ += 2;
            return true;
        }

    private:
        void skipSpaceAndComments() {
            for (;;) {
                while (pos_ < bytes_.size() && std::isspace(bytes_[pos_])) ++pos_;
                if (pos_ >= bytes_.size() || bytes_[pos_] != '#') return;
                while (pos_ < bytes_.size() && bytes_[pos_] != '\n' && bytes_[pos_] != '\r') ++pos_;
            }
        }

        const std::vector<uint8_t>& bytes_;
        std::size_t pos_ = 0;
    };

    bool decodePnm(const std::vector<uint8_t>& b, std::string& error) {
        TokenReader reader(b);
        const std::string magic = reader.next();
        const bool pbm = magic == "P1" || magic == "P4";
        const bool ascii = magic == "P1" || magic == "P2" || magic == "P3";
        const bool gray = magic == "P2" || magic == "P5";
        const bool binary = magic == "P4" || magic == "P5" || magic == "P6";
        if (!ascii && !binary) { error = "Unsupported PNM encoding."; return false; }

        int w = 0, h = 0, maxValue = 1;
        if (!parsePositive(reader.next(), w) || !parsePositive(reader.next(), h) ||
            !validDimensions(static_cast<uint64_t>(w), static_cast<uint64_t>(h))) {
            error = "Invalid PNM dimensions.";
            return false;
        }
        if (!pbm && (!parsePositive(reader.next(), maxValue) || maxValue < 1 || maxValue > 65535)) {
            error = "Invalid PNM color range.";
            return false;
        }
        if (binary && !reader.consumeBinarySeparator()) {
            error = "Invalid PNM binary header.";
            return false;
        }

        width_ = w;
        height_ = h;
        pixels_.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        if (magic == "P4") {
            const std::size_t rowBytes = (static_cast<std::size_t>(w) + 7) / 8;
            for (int y = 0; y < h; ++y) {
                for (std::size_t byte = 0; byte < rowBytes; ++byte) {
                    uint8_t packed = 0;
                    if (!reader.readByte(packed)) {
                        error = "PNM pixel data is incomplete.";
                        return false;
                    }
                    for (int bit = 0; bit < 8; ++bit) {
                        const int x = static_cast<int>(byte * 8 + static_cast<std::size_t>(bit));
                        if (x >= w) break;
                        // In PBM, 1 is black and 0 is white.
                        const uint8_t shade = ((packed >> (7 - bit)) & 1) ? 0 : 255;
                        pixels_[static_cast<std::size_t>(y) * w + x] = {shade, shade, shade};
                    }
                }
            }
            return true;
        }
        const int channels = (pbm || gray) ? 1 : 3;
        for (std::size_t i = 0; i < pixels_.size(); ++i) {
            uint16_t sample[3] = {0, 0, 0};
            for (int c = 0; c < channels; ++c) {
                if (ascii) {
                    const std::string token = reader.next();
                    int n = 0;
                    if (!parsePositive(token, n) || n > (pbm ? 1 : maxValue)) {
                        error = "PNM pixel data is incomplete or invalid.";
                        return false;
                    }
                    sample[c] = static_cast<uint16_t>(n);
                } else if (maxValue < 256) {
                    uint8_t n = 0;
                    if (!reader.readByte(n) || n > maxValue) {
                        error = "PNM pixel data is incomplete or invalid.";
                        return false;
                    }
                    sample[c] = n;
                } else {
                    if (!reader.readWord(sample[c]) || sample[c] > maxValue) {
                        error = "PNM pixel data is incomplete or invalid.";
                        return false;
                    }
                }
            }
            if (pbm) {
                const uint8_t shade = sample[0] == 1 ? 0 : 255;
                pixels_[i] = {shade, shade, shade};
                continue;
            }
            if (gray) sample[1] = sample[2] = sample[0];
            const auto scale = [maxValue](uint16_t n) {
                return static_cast<uint8_t>((static_cast<uint32_t>(n) * 255u +
                                             static_cast<uint32_t>(maxValue) / 2u) /
                                            static_cast<uint32_t>(maxValue));
            };
            pixels_[i] = {scale(sample[0]), scale(sample[1]), scale(sample[2])};
        }
        return true;
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<RgbPixel> pixels_;
};

} // namespace fenrir::apps
