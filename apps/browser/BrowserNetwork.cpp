#include "BrowserNetwork.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stop_token>

#include <curl/curl.h>

#include "litehtml/url.h"

namespace fenrir::apps::browser {
namespace {

struct CurlGlobal {
    CurlGlobal() { ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }
    ~CurlGlobal() { if (ready) curl_global_cleanup(); }
    bool ready = false;
};

CurlGlobal& curlGlobal() {
    static CurlGlobal instance;
    return instance;
}

struct Download {
    std::string body;
    std::size_t limit = 0;
    bool tooLarge = false;
    std::stop_token stop;
};

size_t writeBody(char* data, size_t size, size_t count, void* context) {
    auto& download = *static_cast<Download*>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const std::size_t bytes = size * count;
    if (bytes > download.limit - std::min(download.limit, download.body.size())) {
        download.tooLarge = true;
        return 0;
    }
    download.body.append(data, bytes);
    return bytes;
}

int transferProgress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto& stop = *static_cast<std::stop_token*>(context);
    return stop.stop_requested() ? 1 : 0;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool allowed(const std::string& value) {
    const std::string scheme = lower(litehtml::url(value).scheme());
    return scheme == "http" || scheme == "https";
}

} // namespace

std::string normalizeUrl(const std::string& input) {
    const std::size_t first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = input.find_last_not_of(" \t\r\n");
    std::string value = input.substr(first, last - first + 1);
    const std::size_t schemeEnd = value.find("://");
    if (schemeEnd != std::string::npos) {
        const std::string scheme = lower(value.substr(0, schemeEnd));
        if (scheme != "http" && scheme != "https") return {};
    } else {
        const std::size_t colon = value.find(':');
        if (colon != std::string::npos) {
            const std::string prefix = lower(value.substr(0, colon));
            if (prefix == "http" || prefix == "https") {
                value.insert(colon, "//");
            } else {
                const std::size_t end = value.find_first_of("/?#", colon + 1);
                const std::string port = value.substr(colon + 1,
                    (end == std::string::npos ? value.size() : end) - colon - 1);
                if (port.empty() || !std::all_of(port.begin(), port.end(),
                        [](unsigned char c) { return std::isdigit(c); }))
                    return {};
                value = "https://" + value;
            }
        } else {
            value = "https://" + value;
        }
    }
    const litehtml::url parsed(value);
    return allowed(value) && parsed.has_authority() ? parsed.str() : std::string{};
}

std::string resolveUrl(const std::string& base, const std::string& reference) {
    if (reference.empty()) return {};
    CURLU* handle = curl_url();
    if (!handle) return {};
    CURLUcode result = curl_url_set(handle, CURLUPART_URL, base.c_str(), 0);
    if (result == CURLUE_OK)
        result = curl_url_set(handle, CURLUPART_URL, reference.c_str(), 0);
    char* resolved = nullptr;
    if (result == CURLUE_OK) result = curl_url_get(handle, CURLUPART_URL, &resolved, 0);
    std::string value = result == CURLUE_OK && resolved ? resolved : "";
    curl_free(resolved);
    curl_url_cleanup(handle);
    return allowed(value) && litehtml::url(value).has_authority() ? value : std::string{};
}

Response fetch(const std::string& url, std::size_t maxBytes, long timeoutMs,
               std::stop_token stop) {
    Response response;
    response.requestedUrl = normalizeUrl(url);
    if (response.requestedUrl.empty()) {
        response.error = "Only HTTP and HTTPS URLs are supported.";
        return response;
    }
    if (!curlGlobal().ready) {
        response.error = "HTTP client initialization failed.";
        return response;
    }

    CURL* handle = curl_easy_init();
    if (!handle) {
        response.error = "Could not create HTTP request.";
        return response;
    }

    Download download;
    download.limit = maxBytes;
    download.stop = stop;
    curl_easy_setopt(handle, CURLOPT_URL, response.requestedUrl.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeoutMs, 3000L));
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "FenrirBrowser/0.1");
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, transferProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &download.stop);

    const CURLcode result = curl_easy_perform(handle);
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
    char* effectiveUrl = nullptr;
    char* contentType = nullptr;
    curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &contentType);
    if (effectiveUrl) response.finalUrl = effectiveUrl;
    if (contentType) response.contentType = contentType;
    curl_easy_cleanup(handle);

    if (download.tooLarge) {
        response.error = "Page or resource exceeds the download limit.";
    } else if (result != CURLE_OK) {
        response.error = curl_easy_strerror(result);
    } else if (response.status < 200 || response.status >= 400) {
        response.error = "HTTP status " + std::to_string(response.status);
    } else {
        response.body = std::move(download.body);
    }
    return response;
}

} // namespace fenrir::apps::browser
