#include "api/youtube.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>

#include "api/tsvitch/util/http.hpp"

namespace tsvitch::youtube {

// The client YTB Player uses: as of 2026 the only one that still gets 720p and 1080p (as HLS) without a PO token.
// The player request needs a visitor id in its context.
static const char* const VISION_USER_AGENT =
    "com.google.ios.youtube/1.02 (RealityDevice17,1; U; CPU visionOS 26_5 like Mac OS X)";
static const char* const WEB_USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
const char* const PLAYER_USER_AGENT = "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
const char* const PLAYER_HEADER     = "Origin: https://www.youtube.com";

namespace {

std::atomic<bool> stopping{false};

cpr::Response request(const std::string& url, const cpr::Header& header, const std::string* body = nullptr) {
    cpr::Session session;
    session.SetUrl(cpr::Url{url});
    session.SetTimeout(cpr::Timeout{15000});
    session.SetConnectTimeout(cpr::ConnectTimeout{8000});
    session.SetVerifySsl(HTTP::VERIFY);
    session.SetProxies(cpr::Proxies{HTTP::PROXIES});
    session.SetHeader(header);
    session.SetProgressCallback(cpr::ProgressCallback(
        [](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool {
            return !stopping.load();
        }));
    if (!body) return session.Get();
    session.SetBody(cpr::Body{*body});
    return session.Post();
}

std::string failure(const cpr::Response& r) {
    if (r.error) return r.error.message;
    return "HTTP " + std::to_string(r.status_code);
}

std::string text(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// The visitor id of an anonymous YouTube visit; one lasts a long time, so it is kept for 30 minutes (each request
/// costs a TLS handshake on the Switch)
std::string visitorData(bool fresh, std::string& error) {
    static std::mutex mutex;
    static std::string kept;
    static std::chrono::steady_clock::time_point keptAt;
    std::lock_guard<std::mutex> lock(mutex);
    auto now = std::chrono::steady_clock::now();
    if (!fresh && !kept.empty() && now - keptAt < std::chrono::minutes(30)) return kept;

    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::mt19937 random(static_cast<uint32_t>(now.time_since_epoch().count()));
    std::string visitor(11, 'A');
    for (char& c : visitor) c = alphabet[random() % 64];
    auto r = request("https://www.youtube.com/sw.js_data",
                     cpr::Header{{"User-Agent", WEB_USER_AGENT},
                                 {"Accept", "*/*"},
                                 {"Accept-Language", "en-US"},
                                 {"Referer", "https://www.youtube.com/sw.js"},
                                 {"Cookie", "VISITOR_INFO1_LIVE=" + visitor + ";"}});
    if (r.status_code != 200) {
        error = "visitor id: " + failure(r);
        return {};
    }
    // A JSON array after a guard line: the id is [0][2][0][0][13]
    size_t start = r.text.find('[');
    auto json    = nlohmann::json::parse(start == std::string::npos ? std::string{} : r.text.substr(start), nullptr,
                                         false);
    const nlohmann::json* node = &json;
    for (size_t index : {0, 2, 0, 0}) {
        if (!node->is_array() || node->size() <= index) {
            node = nullptr;
            break;
        }
        node = &(*node)[index];
    }
    if (!node || !node->is_array() || node->size() <= 13 || !(*node)[13].is_string()) {
        error = "visitor id missing";
        return {};
    }
    kept   = (*node)[13].get<std::string>();
    keptAt = now;
    return kept;
}

/// The attributes of an HLS tag line ("#EXT-X-MEDIA:TYPE=AUDIO,NAME=\"x\",..."), quotes removed
std::map<std::string, std::string> attributes(const std::string& line) {
    std::map<std::string, std::string> out;
    size_t at = line.find(':');
    if (at == std::string::npos) return out;
    at++;
    while (at < line.size()) {
        size_t equals = line.find('=', at);
        if (equals == std::string::npos) break;
        std::string key = line.substr(at, equals - at);
        size_t next;
        if (equals + 1 < line.size() && line[equals + 1] == '"') {
            size_t close = line.find('"', equals + 2);
            if (close == std::string::npos) close = line.size();
            out[key] = line.substr(equals + 2, close - equals - 2);
            next     = line.find(',', close);
        } else {
            next     = line.find(',', equals + 1);
            out[key] = line.substr(equals + 1, next == std::string::npos ? std::string::npos : next - equals - 1);
        }
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return out;
}

std::string absolute(const std::string& base, const std::string& uri) {
    if (uri.empty() || uri.find("://") != std::string::npos) return uri;
    size_t scheme = base.find("://");
    if (scheme == std::string::npos) return uri;
    if (uri[0] == '/') return base.substr(0, base.find('/', scheme + 3)) + uri;
    return base.substr(0, base.rfind('/') + 1) + uri;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

struct Variant {
    std::string line;  // the #EXT-X-STREAM-INF line
    std::string uri;
    std::string audioGroup;
    int height    = 0;  // the short side, so a vertical video counts like a wide one
    int bandwidth = 0;
    bool avc      = false;
};

struct Rendition {
    std::string line;  // the #EXT-X-MEDIA line
    std::string group, name, language, contentId, uri;
    bool isDefault = false;
};

/// A tag line with its URI attribute replaced
std::string withUri(const std::string& line, const std::string& uri) {
    size_t start = line.find("URI=\"");
    if (start == std::string::npos) return line;
    start += 5;
    size_t end = line.find('"', start);
    if (end == std::string::npos) return line;
    return line.substr(0, start) + uri + line.substr(end);
}

/// The variant to play: H.264 (the Switch decodes it in hardware) as tall as allowed, else the smallest taller one
const Variant* pickVariant(const std::vector<Variant>& variants, int maxHeight) {
    bool anyAvc = std::any_of(variants.begin(), variants.end(), [](const Variant& v) { return v.avc; });
    const Variant* best = nullptr;
    for (const auto& v : variants) {
        if (anyAvc && !v.avc) continue;
        if (!best) {
            best = &v;
            continue;
        }
        bool fits = v.height <= maxHeight, bestFits = best->height <= maxHeight;
        if (fits != bestFits) {
            if (fits) best = &v;
            continue;
        }
        if (v.height != best->height) {
            // Among those that fit the tallest, among those that do not the smallest
            if (fits ? v.height > best->height : v.height < best->height) best = &v;
        } else if (v.bandwidth > best->bandwidth) {
            best = &v;  // the same size with the better sound group
        }
    }
    return best;
}

/// The sound of a variant's group: the default one, else the original track (YouTube adds automatic dubs, marked
/// "dubbed-auto", without a default), else English, else the first
const Rendition* pickRendition(const std::vector<Rendition>& renditions, const std::string& group) {
    const Rendition *original = nullptr, *english = nullptr, *first = nullptr;
    for (const auto& r : renditions) {
        if (r.group != group) continue;
        if (r.isDefault) return &r;
        std::string name = lower(r.name), language = lower(r.language), content = lower(r.contentId);
        if (!original && (name.find("original") != std::string::npos || content.find("original") != std::string::npos ||
                          content.find(".4") != std::string::npos))
            original = &r;
        if (!english && (language == "en" || language.rfind("en-", 0) == 0 || name.find("english") != std::string::npos))
            english = &r;
        if (!first) first = &r;
    }
    return original ? original : english ? english : first;
}

/// One try: the player request and the master playlist. final: YouTube says the video cannot play
bool resolveOnce(const std::string& id, const std::string& visitor, int maxHeight, Stream& stream, std::string& error,
                 bool& final) {
    nlohmann::json body = {
        {"videoId", id},
        {"contentCheckOk", true},
        {"racyCheckOk", true},
        {"context",
         {{"client",
           {{"clientName", "VISIONOS"},
            {"clientVersion", "1.02"},
            {"deviceMake", "Apple"},
            {"deviceModel", "RealityDevice17,1"},
            {"osName", "visionOS"},
            {"osVersion", "26.5.23O471"},
            {"hl", "en"},
            {"gl", "US"},
            {"visitorData", visitor}}}}},
    };
    std::string payload = body.dump();
    auto r              = request("https://www.youtube.com/youtubei/v1/player?prettyPrint=false",
                                  cpr::Header{{"Content-Type", "application/json"},
                                              {"User-Agent", VISION_USER_AGENT},
                                              {"X-Youtube-Client-Name", "101"},
                                              {"X-Youtube-Client-Version", "1.02"},
                                              {"Origin", "https://www.youtube.com"}},
                                  &payload);
    if (r.status_code != 200) {
        error = "player: " + failure(r);
        return false;
    }
    auto root = nlohmann::json::parse(r.text, nullptr, false);
    if (!root.is_object()) {
        error = "player: no JSON";
        return false;
    }
    if (auto status = root.find("playabilityStatus"); status != root.end() && status->is_object()) {
        std::string state = text(*status, "status");
        if (!state.empty() && state != "OK") {
            std::string reason = text(*status, "reason");
            error              = reason.empty() ? state : reason;
            final              = true;
            return false;
        }
    }
    std::string manifest;
    if (auto streaming = root.find("streamingData"); streaming != root.end() && streaming->is_object())
        manifest = text(*streaming, "hlsManifestUrl");
    if (manifest.empty()) {
        error = "no HLS playlist";
        return false;
    }

    auto m = request(manifest, cpr::Header{{"User-Agent", PLAYER_USER_AGENT}});
    if (m.status_code != 200 || m.text.empty()) {
        error = "master playlist: " + failure(m);
        return false;
    }
    std::vector<Variant> variants;
    std::vector<Rendition> renditions;
    std::istringstream lines(m.text);
    std::string line;
    bool variantNext = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#EXT-X-MEDIA:", 0) == 0) {
            auto a = attributes(line);
            if (a["TYPE"] != "AUDIO" || a["URI"].empty()) continue;
            renditions.push_back({line, a["GROUP-ID"], a["NAME"], a["LANGUAGE"], a["YT-EXT-AUDIO-CONTENT-ID"],
                                  absolute(manifest, a["URI"]), a["DEFAULT"] == "YES"});
        } else if (line.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
            auto a = attributes(line);
            Variant v;
            v.line           = line;
            v.audioGroup     = a["AUDIO"];
            v.avc            = a["CODECS"].find("avc1") != std::string::npos;
            v.bandwidth      = std::atoi(a["BANDWIDTH"].c_str());
            std::string size = a["RESOLUTION"];
            size_t x         = size.find('x');
            if (x != std::string::npos) {
                int width = std::atoi(size.c_str()), height = std::atoi(size.c_str() + x + 1);
                v.height  = width > 0 && height > 0 ? std::min(width, height) : height;
            }
            variants.push_back(v);
            variantNext = true;
        } else if (variantNext && !line.empty() && line[0] != '#') {
            variants.back().uri = absolute(manifest, line);
            variantNext         = false;
        }
    }
    variants.erase(std::remove_if(variants.begin(), variants.end(), [](const Variant& v) { return v.uri.empty(); }),
                   variants.end());
    const Variant* variant = pickVariant(variants, maxHeight);
    if (!variant) {
        error = "no variant in the master playlist";
        return false;
    }
    const Rendition* sound = variant->audioGroup.empty() ? nullptr : pickRendition(renditions, variant->audioGroup);
    stream.playlist = "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n" + variant->line + "\n" + variant->uri + "\n";
    if (sound) stream.playlist += withUri(sound->line, sound->uri) + "\n";
    stream.height = variant->height;
    brls::Logger::info("YouTube: {} {}p {} (sound: {})", id, variant->height, variant->avc ? "H.264" : "other codec",
                       sound ? sound->name : std::string{"in the picture"});
    return true;
}

}  // namespace

std::string videoId(const std::string& value) {
    auto valid = [](const std::string& id) {
        return id.size() == 11 && std::all_of(id.begin(), id.end(), [](char c) {
                   return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
               });
    };
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    std::string trimmed = value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
    for (const char* marker : {"v=", "youtu.be/", "/embed/", "/shorts/"}) {
        size_t at = trimmed.find(marker);
        if (at == std::string::npos) continue;
        std::string id = trimmed.substr(at + std::strlen(marker), 11);
        return valid(id) ? id : std::string{};
    }
    return valid(trimmed) ? trimmed : std::string{};
}

bool resolve(const std::string& id, int maxHeight, Stream& stream, std::string& error) {
    if (id.empty()) {
        error = "no video";
        return false;
    }
    // One more try with a new visitor id when something failed on the way
    for (int attempt = 0; attempt < 2 && !stopping; attempt++) {
        std::string visitor = visitorData(attempt > 0, error);
        if (visitor.empty()) continue;
        bool final = false;
        if (resolveOnce(id, visitor, maxHeight, stream, error, final)) return true;
        if (final) break;
    }
    brls::Logger::warning("YouTube: {} cannot play: {}", id, error);
    return false;
}

void stopRequests() { stopping = true; }

}  // namespace tsvitch::youtube
