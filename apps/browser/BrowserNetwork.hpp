#pragma once

#include <cstddef>
#include <stop_token>
#include <string>

namespace fenrir::apps::browser {

struct Response {
    std::string requestedUrl;
    std::string finalUrl;
    std::string contentType;
    std::string body;
    std::string error;
    long status = 0;
};

std::string normalizeUrl(const std::string& input);
std::string resolveUrl(const std::string& base, const std::string& reference);
Response fetch(const std::string& url, std::size_t maxBytes = 4 * 1024 * 1024,
               long timeoutMs = 8000, std::stop_token stop = {});

// In-memory cookie jar helpers (per-host).
std::string getCookieString(const std::string& url);
void setCookieString(const std::string& url, const std::string& cookie);

} // namespace fenrir::apps::browser
