#include <atomic>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>
#include <vector>
#include <algorithm>
#include <regex>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <numeric>
#include <iterator>
#include <cstdlib>
#include <memory>

#include "tsvitch.h"
#include "tsvitch/util/http.hpp"
#include "borealis/core/thread.hpp"
#include "borealis/core/application.hpp"

#include "tsvitch/result/home_live_result.h"
#include "tsvitch/result/xtream_detail.h"
#include "utils/text_fold.hpp"
#include "utils/config_helper.hpp"

namespace tsvitch {

/// Pulisce il testo rimuovendo caratteri che potrebbero causare problemi di rendering
// Helper function to safely get string value from JSON, handling null values
static std::string safeGetString(const nlohmann::json& json, const std::string& key, const std::string& defaultValue = "") {
    if (!json.contains(key) || json[key].is_null()) {
        return defaultValue;
    }
    if (json[key].is_string()) {
        return json[key].get<std::string>();
    }
    return defaultValue;
}

// Extracts a field that may be a string or a number (e.g. category_id) as a string
static std::string safeGetIdString(const nlohmann::json& json, const std::string& key) {
    if (!json.contains(key) || json[key].is_null()) {
        return "";
    }
    if (json[key].is_string()) {
        return json[key].get<std::string>();
    }
    if (json[key].is_number_integer()) {
        return std::to_string(json[key].get<long long>());
    }
    if (json[key].is_number()) {
        return std::to_string(json[key].get<int>());
    }
    return "";
}

// Helper function to sanitize text for safe font rendering: control characters become spaces,
// runs of spaces collapse into one and the ends are trimmed. A plain loop instead of std::regex,
// which took seconds for the tens of thousands of titles of a movie list on the Switch.
static std::string sanitizeText(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    bool pendingSpace = false;
    for (unsigned char c : text) {
        if (c < 32 || c == 127 || c == ' ') {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !cleaned.empty()) cleaned += ' ';
        pendingSpace = false;
        cleaned += static_cast<char>(c);
    }
    return cleaned;
}

/// Découpe une chaîne `a,b,c` -> {"a","b","c"}
static std::vector<std::string> split_csv(const std::string& csv)
{
    std::vector<std::string> out;
    std::stringstream        ss(csv);
    std::string              item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

nlohmann::json parse_m3u8_to_json(const std::string& m3u8_content)
{
    if (m3u8_content.empty()) {
        return nlohmann::json::array();
    }

    std::istringstream stream(m3u8_content);
    std::string        line;
    nlohmann::json     json_result = nlohmann::json::array();
    nlohmann::json     current_entry;
    
    // Pre-allocazione ottimizzata per migliorare prestazioni
    size_t estimated_channels = std::count(m3u8_content.begin(), m3u8_content.end(), '\n') / 3;
    json_result.get_ref<nlohmann::json::array_t&>().reserve(estimated_channels + 100);

    // Buffer riutilizzabile per evitare allocazioni
    std::string extinf_buffer;
    extinf_buffer.reserve(512);

    while (std::getline(stream, line)) {
        // Trim veloce di \r\n alla fine
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        
        // Controllo ultra-rapido per saltare linee vuote o commenti non EXTINF
        if (line.empty() || (line[0] == '#' && (line.size() < 8 || line.compare(0, 8, "#EXTINF:") != 0))) {
            continue;
        }
        
        if (line.compare(0, 8, "#EXTINF:") == 0) {
            if (!current_entry.empty()) {
                json_result.push_back(std::move(current_entry));
                current_entry.clear();
            }

            extinf_buffer = line.substr(8);
            
            // Parsing ultra-ottimizzato con una sola passata della stringa
            const char* data = extinf_buffer.c_str();
            const size_t len = extinf_buffer.length();
            
            // Cerca i pattern più comuni per primi (ottimizzazione basata su frequenza)
            size_t comma_pos = std::string::npos;
            size_t tvg_id_start = 0, tvg_id_len = 0;
            size_t tvg_chno_start = 0, tvg_chno_len = 0;
            size_t tvg_logo_start = 0, tvg_logo_len = 0;
            size_t group_title_start = 0, group_title_len = 0;
            
            for (size_t i = 0; i < len; ++i) {
                // Cerca comma (titolo) - più frequente
                if (data[i] == ',' && comma_pos == std::string::npos) {
                    comma_pos = i;
                }
                // Cerca tvg-id
                else if (data[i] == 't' && i + 7 < len && memcmp(data + i, "tvg-id=\"", 8) == 0) {
                    i += 8;
                    tvg_id_start = i;
                    while (i < len && data[i] != '"') ++i;
                    tvg_id_len = i - tvg_id_start;
                }
                // Cerca group-title
                else if (data[i] == 'g' && i + 12 < len && memcmp(data + i, "group-title=\"", 13) == 0) {
                    i += 13;
                    group_title_start = i;
                    while (i < len && data[i] != '"') ++i;
                    group_title_len = i - group_title_start;
                }
                // Cerca tvg-chno
                else if (data[i] == 't' && i + 9 < len && memcmp(data + i, "tvg-chno=\"", 10) == 0) {
                    i += 10;
                    tvg_chno_start = i;
                    while (i < len && data[i] != '"') ++i;
                    tvg_chno_len = i - tvg_chno_start;
                }
                // Cerca tvg-logo
                else if (data[i] == 't' && i + 9 < len && memcmp(data + i, "tvg-logo=\"", 10) == 0) {
                    i += 10;
                    tvg_logo_start = i;
                    while (i < len && data[i] != '"') ++i;
                    tvg_logo_len = i - tvg_logo_start;
                }
            }

            // Assegna i valori trovati (solo se non vuoti)
            if (tvg_id_len > 0) {
                current_entry["id"] = std::string(data + tvg_id_start, tvg_id_len);
            }
            if (tvg_chno_len > 0) {
                current_entry["chno"] = std::string(data + tvg_chno_start, tvg_chno_len);
            }
            if (tvg_logo_len > 0) {
                current_entry["logo"] = std::string(data + tvg_logo_start, tvg_logo_len);
            }
            if (group_title_len > 0) {
                current_entry["groupTitle"] = sanitizeText(std::string(data + group_title_start, group_title_len));
            }
            if (comma_pos != std::string::npos && comma_pos + 1 < len) {
                current_entry["title"] = sanitizeText(extinf_buffer.substr(comma_pos + 1));
            }
        } else if (!line.empty() && line[0] != '#') {
            current_entry["url"] = std::move(line);
        }
    }

    // Aggiungi l'ultimo entry se presente e valido
    if (!current_entry.empty() && current_entry.contains("id")) {
        json_result.push_back(std::move(current_entry));
    }
    

#ifdef DEBUG
    std::cout << "json_result:\n" << json_result.dump(2) << std::endl;
#endif
    return json_result;
}

void TsVitchClient::get_file_m3u8(const std::function<void(LiveM3u8ListResult)>& callback,
                                  const ErrorCallback&                           error)
{
    auto m3u8Url = ProgramConfig::instance().getM3U8Url();
    auto timeoutMs = ProgramConfig::instance().getIntOption(SettingItem::M3U8_TIMEOUT);
    
    // Timeout più intelligente basato sulla dimensione prevista
    if (timeoutMs < 30000) timeoutMs = 30000; // Minimum 30 secondi per file M3U8 grandi
    
    brls::Logger::info("Fetching M3U8 playlist from: {} (timeout: {}ms)", m3u8Url, timeoutMs);
    
    HTTP::__cpr_get(
        m3u8Url,
        {},
        timeoutMs,
        [callback, error](const cpr::Response& r) {
            // Log dimensione risposta per debug prestazioni
            brls::Logger::info("M3U8 download completed - Size: {} bytes, Status: {}", r.text.size(), r.status_code);
            
#ifdef __SWITCH__
            // Per file molto grandi, usa sempre parsing asincrono anche su Switch
            bool useAsyncParsing = r.text.size() > 1024 * 1024; // 1MB threshold
            
            // Switch: usa parsing sincrono per file piccoli, asincrono per file grandi
            if (useAsyncParsing) {
                brls::Logger::info("Large M3U8 file detected ({}MB), using async parsing on Switch", r.text.size() / (1024*1024));
                
                // Create a cancellation token 
                auto cancellationToken = std::make_shared<std::atomic<bool>>(false);
                
                // Subscribe to exit event to cancel parsing
                auto exitSubscription = brls::Application::getExitEvent()->subscribe([cancellationToken]() {
                    brls::Logger::info("M3U8 parsing: Exit event received, setting cancellation flag");
                    cancellationToken->store(true);
                });
                
                brls::Threading::async([callback, error, responseText = std::move(r.text), cancellationToken, exitSubscription]() {
                    try {
                        // Check if parsing should be canceled
                        if (cancellationToken->load()) {
                            brls::Logger::info("M3U8 async parsing canceled before start - application is exiting");
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                            return;
                        }
                        
                        auto start_time = std::chrono::high_resolution_clock::now();
                        nlohmann::json json_result = parse_m3u8_to_json(responseText);
                        
                        // Check again if parsing should be canceled
                        if (cancellationToken->load()) {
                            brls::Logger::info("M3U8 async parsing canceled during execution - application is exiting");
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                            return;
                        }
                        
                        auto end_time = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                        brls::Logger::info("Switch async M3U8 parsing completed in {}ms, found {} channels", duration.count(), json_result.size());

                        LiveM3u8ListResult result;
                        result.reserve(json_result.size());
                        
                        for (const auto& item : json_result) {
                            LiveM3u8 live;
                            live.id         = item.value("id", "");
                            live.chno       = item.value("chno", "");
                            live.logo       = item.value("logo", "");
                            {
                                std::string groupTitle = item.value("groupTitle", "");
                                std::replace(groupTitle.begin(), groupTitle.end(), ';', ' ');
                                live.groupTitle = sanitizeText(groupTitle);
                            }
                            live.title      = sanitizeText(item.value("title", ""));
                            live.url        = item.value("url", "");
                            result.push_back(std::move(live));
                        }
                        
                        // Final check before calling callback
                        if (cancellationToken->load()) {
                            brls::Logger::info("M3U8 async parsing canceled before callback - application is exiting");
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                            return;
                        }
                        
                        brls::sync([callback, result = std::move(result), cancellationToken, exitSubscription]() {
                            // Check once more in sync context
                            if (cancellationToken->load()) {
                                brls::Logger::info("M3U8 sync callback canceled - application is exiting");
                                brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                                return;
                            }
                            CALLBACK(result);
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                        });
                    } catch (const std::exception& e) {
                        brls::Logger::error("Switch async M3U8 parsing error: {}", e.what());
                        
                        // Don't call error callback if app is exiting
                        if (cancellationToken->load()) {
                            brls::Logger::info("M3U8 async error callback canceled - application is exiting");
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                            return;
                        }
                        
                        brls::sync([error, cancellationToken, exitSubscription]() {
                            if (cancellationToken->load()) {
                                brls::Logger::info("M3U8 sync error callback canceled - application is exiting");
                                brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                                return;
                            }
                            ERROR_MSG("cannot get file m3u8", -1);
                            error("Failed to parse m3u8 content", -1);
                            brls::Application::getExitEvent()->unsubscribe(exitSubscription);
                        });
                    }
                });
            } else {
                // File piccoli: parsing sincrono ottimizzato
                try {
                    auto start_time = std::chrono::high_resolution_clock::now();
                    nlohmann::json json_result = parse_m3u8_to_json(r.text);
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    brls::Logger::info("Switch sync M3U8 parsing completed in {}ms, found {} channels", duration.count(), json_result.size());

                    LiveM3u8ListResult result;
                    result.reserve(json_result.size());
                    
                    for (const auto& item : json_result) {
                        LiveM3u8 live;
                        live.id         = item.value("id", "");
                        live.chno       = item.value("chno", "");
                        live.logo       = item.value("logo", "");
                        {
                            std::string groupTitle = item.value("groupTitle", "");
                            std::replace(groupTitle.begin(), groupTitle.end(), ';', ' ');
                            live.groupTitle = sanitizeText(groupTitle);
                        }
                        live.title      = sanitizeText(item.value("title", ""));
                        live.url        = item.value("url", "");
                        result.push_back(std::move(live));
                    }
                    
                    CALLBACK(result);
                } catch (const std::exception& e) {
                    brls::Logger::error("Switch sync M3U8 parsing error: {}", e.what());
                    ERROR_MSG("cannot get file m3u8", -1);
                    error("Failed to parse m3u8 content", -1);
                }
            }
#else
            // Altre piattaforme: sempre parsing asincrono per migliori prestazioni
            brls::Threading::async([callback, error, responseText = std::move(r.text)]() {
                try {
                    auto start_time = std::chrono::high_resolution_clock::now();
                    nlohmann::json json_result = parse_m3u8_to_json(responseText);
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    brls::Logger::info("M3U8 parsing completed in {}ms, found {} channels", duration.count(), json_result.size());

                    LiveM3u8ListResult result;
                    result.reserve(json_result.size());
                    
                    for (const auto& item : json_result) {
                        LiveM3u8 live;
                        live.id         = item.value("id", "");
                        live.chno       = item.value("chno", "");
                        live.logo       = item.value("logo", "");
                        {
                            std::string groupTitle = item.value("groupTitle", "");
                            std::replace(groupTitle.begin(), groupTitle.end(), ';', ' ');
                            live.groupTitle = sanitizeText(groupTitle);
                        }
                        live.title      = sanitizeText(item.value("title", ""));
                        live.url        = item.value("url", "");
                        result.push_back(std::move(live));
                    }
                    
                    brls::sync([callback, result = std::move(result)]() {
                        CALLBACK(result);
                    });
                } catch (const std::exception& e) {
                    brls::Logger::error("M3U8 parsing error: {}", e.what());
                    brls::sync([error]() {
                        ERROR_MSG("cannot get file m3u8", -1);
                        error("Failed to parse m3u8 content", -1);
                    });
                }
            });
#endif
        },
        error);
}

void TsVitchClient::get_xtream_channels(const std::function<void(LiveM3u8ListResult)>& callback,
                                       const ErrorCallback& error) {
    get_xtream_channels_with_retry(callback, error, 3); // Max 3 retry attempts
}

// ---------------------------------------------------------------------------------------------
// Xtream Codes
// ---------------------------------------------------------------------------------------------

/**
 * Describes which Xtream content to fetch. Live TV, movies and series share the same
 * fetch/parse pipeline and only differ in the API actions and in how items become URLs.
 */
struct XtreamContentKind {
    std::string categoriesAction;    // get_live_categories / get_vod_categories / get_series_categories
    std::string streamsAction;       // get_live_streams / get_vod_streams / get_series
    std::string urlSegment;          // "live" / "movie" (unused for series)
    bool useContainerExtension;      // false -> ".ts"; true -> item.container_extension (fallback mp4)
    std::string fallbackGroupTitle;  // group name when the category cannot be resolved
    std::string label;               // used only in logs
    bool isSeriesList;               // true -> series list (id=series_id, no player url)
    int contentType;                 // 0 = live, 1 = movie, 2 = series
};

// URL scheme used as a sentinel for series items in the list (clicking opens the episodes)
static const std::string XTREAM_SERIES_SCHEME = "xtream-series://";

static const XtreamContentKind XTREAM_LIVE{
    "get_live_categories", "get_live_streams", "live", false, "Live TV", "live", false, 0};
static const XtreamContentKind XTREAM_MOVIES{
    "get_vod_categories", "get_vod_streams", "movie", true, "Movies", "vod", false, 1};
static const XtreamContentKind XTREAM_SERIES{
    "get_series_categories", "get_series", "series", true, "Series", "series", true, 2};

/// Categories of one content type, in the order the server lists them
struct XtreamCategories {
    std::unordered_map<std::string, std::string> names;  // category_id -> category_name
    std::unordered_map<std::string, size_t> rank;        // category_id -> position in the server's list
};
using XtreamCategoryMap = std::shared_ptr<XtreamCategories>;

struct XtreamAccount {
    std::string baseUrl;  // server url ending with '/'
    std::string username;
    std::string password;
    int32_t timeoutMs = 45000;
};

/// Reads the account from the settings; returns false when it is incomplete
static bool getXtreamAccount(XtreamAccount& account) {
    account.baseUrl  = ProgramConfig::instance().getXtreamServerUrl();
    account.username = ProgramConfig::instance().getXtreamUsername();
    account.password = ProgramConfig::instance().getXtreamPassword();
    if (account.baseUrl.empty() || account.username.empty() || account.password.empty()) return false;
    if (account.baseUrl.back() != '/') account.baseUrl += "/";
    account.timeoutMs = std::max<int32_t>(ProgramConfig::instance().getIntOption(SettingItem::M3U8_TIMEOUT), 45000);
    return true;
}

/// Xtream sends numbers either as JSON numbers or as strings
static double safeGetNumber(const nlohmann::json& json, const std::string& key) {
    auto it = json.find(key);
    if (it == json.end() || it->is_null()) return 0;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        const auto& text = it->get_ref<const std::string&>();
        char* end        = nullptr;
        double value     = std::strtod(text.c_str(), &end);
        return end != text.c_str() ? value : 0;
    }
    return 0;
}

/// Requests of one kind go out one at a time with a short gap. Lists (big and slow) and everything else
/// (information screens, account, programme guide) use separate lanes, so a screen never waits behind a
/// list download.
struct XtreamLane {
    std::mutex mutex;
    std::chrono::steady_clock::time_point lastRequest;
};
static XtreamLane xtreamListLane, xtreamInfoLane;

/// Set when the app closes: waiting requests give up at once (closing waits for cpr's threads)
static std::atomic<bool> xtreamStopping{false};

void TsVitchClient::stopRequests() {
    xtreamStopping = true;
    brls::Logger::info("Xtream: requests stopped");
}

/// Sleeps in short steps; false when the app is closing
static bool waitUnlessStopping(std::chrono::steady_clock::duration wait) {
    auto until = std::chrono::steady_clock::now() + wait;
    while (std::chrono::steady_clock::now() < until) {
        if (xtreamStopping) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !xtreamStopping;
}

/// The server sometimes closes the connection cleanly in the middle of a big list: a body that does not
/// start and end like a JSON array/object was cut off and is retried like a network error
static bool looksLikeCompleteJson(const std::string& body) {
    auto first = body.find_first_not_of(" \t\r\n");
    auto last  = body.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    return (body[first] == '[' && body[last] == ']') || (body[first] == '{' && body[last] == '}');
}

static bool isRetryableXtreamResponse(const cpr::Response& r) {
    if (r.error || r.status_code == 0 || r.status_code == 429 || r.status_code == 461 || r.status_code >= 500)
        return true;
    return r.status_code == 200 && !looksLikeCompleteJson(r.text);
}

/**
 * Calls player_api.php on cpr's thread pool and hands the response to `done` on that thread.
 * Some servers drop requests that arrive in quick succession (connection reset or HTTP 461 from
 * their flood protection), so each lane sends one request at a time with a short gap and retries
 * network errors, 429, 461 and 5xx with a growing delay.
 * list: a big list (long timeout, more retries); otherwise a small request that must answer soon, so a
 * server that does not respond shows an error after about a minute instead of several.
 * The credentials travel only as query parameters and are never logged.
 */
namespace {

std::function<void(const XtreamLoadState&)> xtreamLoadObserver;

/// Hands a list download state to the screen. The observer is read on the UI thread when the state arrives
/// there, so a screen that closed in the meantime gets nothing.
void notifyXtreamLoad(const XtreamLoadState& state) {
    if (state.contentType < 0) return;
    brls::sync([state]() {
        if (xtreamLoadObserver) xtreamLoadObserver(state);
    });
}

}  // namespace

void TsVitchClient::setXtreamLoadObserver(std::function<void(const XtreamLoadState&)> observer) {
    xtreamLoadObserver = std::move(observer);
}

// reportType / reportPhase: the list (0 live, 1 movies, 2 series) whose download screen hears about this
// request, and what the request is for it; -1 reports nothing
static void xtreamApiGet(const XtreamAccount& account, const std::string& action,
                         const std::vector<std::pair<std::string, std::string>>& extra,
                         std::function<void(cpr::Response)> done, bool list = false, int reportType = -1,
                         XtreamLoadState::Phase reportPhase = XtreamLoadState::CATEGORIES) {
    cpr::async([account, action, extra, done, list, reportType, reportPhase]() {
        static const int listRetryMs[] = {1500, 3000, 5000, 8000};
        static const int infoRetryMs[] = {1500, 3000};
        const int* retryDelaysMs       = list ? listRetryMs : infoRetryMs;
        const size_t retries           = list ? std::size(listRetryMs) : std::size(infoRetryMs);
        const int32_t timeoutMs        = list ? account.timeoutMs : 20000;
        auto& lane                     = list ? xtreamListLane : xtreamInfoLane;
        constexpr auto minGap          = std::chrono::milliseconds(800);

        cpr::Parameters params{{"username", account.username}, {"password", account.password}};
        if (!action.empty()) params.Add(cpr::Parameter{"action", action});
        for (const auto& [key, value] : extra) params.Add(cpr::Parameter{key, value});

        auto report = [reportType](XtreamLoadState state) {
            state.contentType = reportType;
            notifyXtreamLoad(state);
        };
        cpr::Response r;
        {
            // Another request of the lane is running (usually another list): the screen says it waits
            std::unique_lock<std::mutex> lock(lane.mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                report({0, XtreamLoadState::QUEUED});
                lock.lock();
            }
            for (size_t attempt = 0;; attempt++) {
                if (!waitUnlessStopping(lane.lastRequest + minGap - std::chrono::steady_clock::now())) return;
                report({0, reportPhase});
                auto lastReport = std::make_shared<std::chrono::steady_clock::time_point>();
                r = cpr::Get(cpr::Url{account.baseUrl + "player_api.php"}, params, cpr::Timeout{timeoutMs},
                             cpr::ConnectTimeout{10000}, HTTP::HEADERS, HTTP::COOKIES, HTTP::PROXIES, HTTP::VERIFY,
                             cpr::ProgressCallback([reportType, reportPhase, lastReport](
                                                       cpr::cpr_pf_arg_t total, cpr::cpr_pf_arg_t now,
                                                       cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool {
                                 // the received bytes, four times a second
                                 auto time = std::chrono::steady_clock::now();
                                 if (reportType >= 0 && reportPhase == XtreamLoadState::DOWNLOADING && now > 0 &&
                                     time - *lastReport >= std::chrono::milliseconds(250)) {
                                     *lastReport = time;
                                     XtreamLoadState state{reportType, XtreamLoadState::DOWNLOADING};
                                     state.bytes = static_cast<int64_t>(now);
                                     state.total = static_cast<int64_t>(total);
                                     notifyXtreamLoad(state);
                                 }
                                 return !xtreamStopping.load();
                             }));
                lane.lastRequest = std::chrono::steady_clock::now();
                if (xtreamStopping) return;
                if (!isRetryableXtreamResponse(r) || attempt >= retries) break;
                brls::Logger::warning("Xtream {}: failed (status {}, {} bytes, {}), retry {} in {} ms", action,
                                      r.status_code, r.text.size(), r.error.message, attempt + 1,
                                      retryDelaysMs[attempt]);
                XtreamLoadState retry{0, XtreamLoadState::RETRY};
                retry.attempt        = static_cast<int>(attempt) + 1;
                retry.attempts       = static_cast<int>(retries);
                retry.retryInSeconds = (retryDelaysMs[attempt] + 999) / 1000;
                report(retry);
                if (!waitUnlessStopping(std::chrono::milliseconds(retryDelaysMs[attempt]))) return;
            }
        }
        done(std::move(r));
    });
}

/// Fetches the categories of a content type; on failure the map stays empty and items keep a fallback group
static void fetchXtreamCategories(const XtreamAccount& account, const XtreamContentKind& kind,
                                  std::function<void(XtreamCategoryMap)> done) {
    xtreamApiGet(account, kind.categoriesAction, {}, [done, label = kind.label](cpr::Response r) {
        auto categories = std::make_shared<XtreamCategories>();
        if (!r.error && r.status_code == 200) {
            auto json = nlohmann::json::parse(r.text, nullptr, false);
            if (json.is_array()) {
                for (const auto& item : json) {
                    if (!item.is_object()) continue;
                    std::string id   = safeGetIdString(item, "category_id");
                    std::string name = sanitizeText(safeGetString(item, "category_name"));
                    if (id.empty() || name.empty() || categories->names.count(id)) continue;
                    categories->rank[id]  = categories->names.size();
                    categories->names[id] = name;
                }
            }
        }
        if (categories->names.empty())
            brls::Logger::warning("Xtream {}: no categories (status {}), items keep a fallback group", label,
                                  r.status_code);
        done(categories);
    }, true, kind.contentType, XtreamLoadState::CATEGORIES);
}

// Only these keys of the get_*_streams / get_series items are used. Dropping the rest while parsing
// keeps big movie/series lists (tens of MB of JSON with plots, casts and backdrops) within the
// Switch's memory.
static bool keepXtreamStreamKey(const std::string& key) {
    static const std::unordered_set<std::string> keys = {
        "series_id", "cover",       "stream_id",           "stream_icon", "num",   "name",
        "category_name", "category_id", "container_extension", "rating",      "added", "last_modified",
        "releaseDate",   "release_date",  "year"};
    return keys.count(key) > 0;
}

/// Downloads and parses the list of one content type, then calls back on the UI thread
static void xtreamFetchContent(const std::function<void(LiveM3u8ListResult)>& callback,
                               const ErrorCallback& error, const XtreamContentKind& kind) {
    XtreamAccount account;
    if (!getXtreamAccount(account)) {
        if (error) error("Xtream Codes credentials not configured properly", -1);
        return;
    }

    fetchXtreamCategories(account, kind, [account, kind, callback, error](XtreamCategoryMap categories) {
        xtreamApiGet(account, kind.streamsAction, {}, [account, kind, categories, callback, error](cpr::Response r) {
            auto fail = [error](const std::string& message, int code) {
                brls::Logger::error("Xtream: {}", message);
                brls::sync([error, message, code]() {
                    if (error) error(message, code);
                });
            };
            if (r.error) return fail("Network error: " + r.error.message, -1);
            if (r.status_code != 200) return fail("HTTP error " + std::to_string(r.status_code), r.status_code);
            XtreamLoadState prepared{kind.contentType, XtreamLoadState::PREPARING};
            prepared.bytes = static_cast<int64_t>(r.downloaded_bytes);  // the whole download, for the next time
            notifyXtreamLoad(prepared);

            auto parseStart = std::chrono::steady_clock::now();
            nlohmann::json json;
            try {
                json = nlohmann::json::parse(
                    r.text, [](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
                        // depth 2 = keys of the item objects inside the top-level array
                        if (event == nlohmann::json::parse_event_t::key && depth == 2)
                            return keepXtreamStreamKey(parsed.get<std::string>());
                        return true;
                    });
            } catch (const std::exception& e) {
                brls::Logger::error("Xtream {}: {}", kind.label, e.what());
                return fail(brls::getStr("tsvitch/xtream/bad_response"), -1);
            }
            // The raw JSON can be tens of MB: free it before building the list
            std::string().swap(r.text);
            if (!json.is_array()) return fail(brls::getStr("tsvitch/xtream/bad_response"), -1);

            LiveM3u8ListResult result;
            std::vector<size_t> ranks;
            result.reserve(json.size());
            ranks.reserve(json.size());
            const size_t unknownRank = categories->names.size();
            for (const auto& item : json) {
                if (!item.is_object()) continue;
                LiveM3u8 live;
                live.id    = safeGetIdString(item, kind.isSeriesList ? "series_id" : "stream_id");
                live.title = sanitizeText(safeGetString(item, "name"));
                if (live.id.empty() || live.title.empty()) continue;
                live.logo   = safeGetString(item, kind.isSeriesList ? "cover" : "stream_icon");
                live.chno   = safeGetIdString(item, "num");
                live.type   = kind.contentType;
                live.rating = static_cast<float>(safeGetNumber(item, "rating"));
                live.added  = static_cast<int64_t>(safeGetNumber(item, kind.isSeriesList ? "last_modified" : "added"));
                if (kind.isSeriesList) {
                    live.year = yearFromText(safeGetString(item, "releaseDate"));
                    if (!live.year) live.year = yearFromText(safeGetString(item, "release_date"));
                }
                // Movies: some servers send the year in its own field, others only in the title
                if (!live.year && kind.contentType == 1) live.year = yearFromText(safeGetIdString(item, "year"));
                if (!live.year && kind.contentType != 0) live.year = yearFromText(live.title);

                // get_*_streams only exposes category_id: the name comes from the category list
                std::string categoryId   = safeGetIdString(item, "category_id");
                std::string categoryName = safeGetString(item, "category_name");
                auto name                = categories->names.find(categoryId);
                if (categoryName.empty() && name != categories->names.end()) categoryName = name->second;
                live.groupTitle = sanitizeText(categoryName.empty() ? kind.fallbackGroupTitle : categoryName);
                auto rank       = categories->rank.find(categoryId);
                ranks.push_back(rank != categories->rank.end() ? rank->second : unknownRank);

                if (kind.isSeriesList) {
                    // Sentinel url: selecting a series opens its episodes
                    live.url = XTREAM_SERIES_SCHEME + live.id;
                } else {
                    std::string ext = kind.useContainerExtension ? safeGetString(item, "container_extension") : "ts";
                    if (ext.empty()) ext = "mp4";
                    live.url = account.baseUrl + kind.urlSegment + "/" + account.username + "/" + account.password +
                               "/" + live.id + "." + ext;
                }
                result.push_back(std::move(live));
            }
            json = nullptr;

            // Categories in the server's order; inside a category the server's item order is kept
            std::vector<size_t> order(result.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&ranks](size_t a, size_t b) { return ranks[a] < ranks[b]; });
            auto sorted = std::make_shared<LiveM3u8ListResult>();
            sorted->reserve(result.size());
            for (size_t i : order) sorted->push_back(std::move(result[i]));

            brls::Logger::info("Xtream {}: {} items, {} categories, parsed in {} ms", kind.label, sorted->size(),
                               categories->names.size(),
                               std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - parseStart)
                                   .count());
            brls::sync([callback, sorted]() {
                if (callback) callback(std::move(*sorted));
            });
        }, true, kind.contentType, XtreamLoadState::DOWNLOADING);
    });
}

void TsVitchClient::get_xtream_channels_with_retry(const std::function<void(LiveM3u8ListResult)>& callback,
                                                  const ErrorCallback& error, int) {
    xtreamFetchContent(callback, error, XTREAM_LIVE);
}

void TsVitchClient::get_xtream_vod(const std::function<void(LiveM3u8ListResult)>& callback,
                                   const ErrorCallback& error) {
    xtreamFetchContent(callback, error, XTREAM_MOVIES);
}

void TsVitchClient::get_xtream_series(const std::function<void(LiveM3u8ListResult)>& callback,
                                      const ErrorCallback& error) {
    xtreamFetchContent(callback, error, XTREAM_SERIES);
}

void TsVitchClient::get_xtream_category_names(int contentType,
                                              const std::function<void(std::vector<std::string>)>& callback,
                                              const ErrorCallback& error) {
    const XtreamContentKind& kind = contentType == 2 ? XTREAM_SERIES : contentType == 1 ? XTREAM_MOVIES : XTREAM_LIVE;
    XtreamAccount account;
    if (!getXtreamAccount(account)) {
        if (error) error("Xtream Codes credentials not configured properly", -1);
        return;
    }
    fetchXtreamCategories(account, kind, [callback](XtreamCategoryMap categories) {
        std::vector<std::string> names;
        for (const auto& kv : categories->names) names.push_back(kv.second);
        std::sort(names.begin(), names.end());
        brls::sync([callback, names]() {
            if (callback) callback(names);
        });
    });
}

/// First picture of a backdrop_path field, which is an array, a string or a JSON text of an array
static std::string firstBackdrop(const nlohmann::json& info) {
    auto it = info.find("backdrop_path");
    if (it == info.end()) return "";
    nlohmann::json value = *it;
    if (value.is_string() && !value.get_ref<const std::string&>().empty() &&
        value.get_ref<const std::string&>()[0] == '[')
        value = nlohmann::json::parse(value.get<std::string>(), nullptr, false);
    if (value.is_string()) return value.get<std::string>();
    if (value.is_array()) {
        for (const auto& item : value)
            if (item.is_string() && !item.get_ref<const std::string&>().empty()) return item.get<std::string>();
    }
    return "";
}

/**
 * Seasons and episodes of a get_series_info response
 * { seasons, info, episodes: { "<season>": [ {id, title, episode_num, container_extension, info, ...} ] } }
 * (some servers send `episodes` as an array of seasons instead). Each episode becomes a playable item
 * (url = series/<user>/<pass>/<id>.<ext>) with its still as picture, or `fallbackLogo` when it has none.
 */
static std::vector<XtreamSeason> parseXtreamSeasons(const nlohmann::json& d, const XtreamAccount& account,
                                                    const std::string& fallbackLogo) {
    // Season names (season_number -> name), when available
    std::unordered_map<std::string, std::string> seasonNames;
    if (d.contains("seasons") && d["seasons"].is_array()) {
        for (const auto& s : d["seasons"]) {
            if (!s.is_object()) continue;
            std::string num  = safeGetIdString(s, "season_number");
            std::string name = sanitizeText(safeGetString(s, "name"));
            if (!num.empty() && !name.empty()) seasonNames[num] = name;
        }
    }

    // Safety cap against corrupt responses that could exhaust the memory
    constexpr size_t MAX_EPISODES = 20000;
    size_t total                  = 0;
    std::vector<XtreamSeason> seasons;
    auto addSeason = [&](const std::string& seasonKey, const nlohmann::json& list) {
        if (!list.is_array()) return;
        XtreamSeason season;
        season.number = std::atoi(seasonKey.c_str());
        season.name   = seasonNames.count(seasonKey) ? seasonNames[seasonKey]
                                                     : brls::getStr("tsvitch/xtream/season", seasonKey);
        for (const auto& ep : list) {
            if (!ep.is_object() || total >= MAX_EPISODES) continue;
            std::string id = safeGetIdString(ep, "id");
            if (id.empty()) continue;
            std::string ext = safeGetString(ep, "container_extension");
            if (ext.empty()) ext = "mp4";
            static const nlohmann::json noInfo = nlohmann::json::object();
            const auto& info = ep.contains("info") && ep["info"].is_object() ? ep["info"] : noInfo;

            XtreamEpisode e;
            e.number     = std::atoi(safeGetIdString(ep, "episode_num").c_str());
            e.plot       = sanitizeText(safeGetString(info, "plot"));
            e.duration   = safeGetString(info, "duration");
            e.airDate    = safeGetString(info, "air_date");
            auto& item   = e.item;
            item.id      = id;
            item.chno    = safeGetIdString(ep, "episode_num");
            item.title   = sanitizeText(safeGetString(ep, "title"));
            if (item.title.empty()) item.title = season.name + " - " + item.chno;
            item.groupTitle = season.name;
            item.logo       = safeGetString(info, "movie_image");
            if (item.logo.empty()) item.logo = fallbackLogo;
            item.type   = 2;
            item.rating = static_cast<float>(safeGetNumber(info, "rating"));
            item.added  = static_cast<int64_t>(safeGetNumber(ep, "added"));
            item.url = account.baseUrl + "series/" + account.username + "/" + account.password + "/" + id + "." + ext;
            season.episodes.push_back(std::move(e));
            total++;
        }
        // Episodes in playing order inside the season
        std::stable_sort(season.episodes.begin(), season.episodes.end(),
                         [](const XtreamEpisode& a, const XtreamEpisode& b) { return a.number < b.number; });
        if (!season.episodes.empty()) seasons.push_back(std::move(season));
    };

    const auto& eps = d["episodes"];
    if (eps.is_object()) {
        // Object keys come back sorted as text ("10" before "2"): order the seasons by number
        std::vector<std::string> keys;
        for (auto it = eps.begin(); it != eps.end(); ++it) keys.push_back(it.key());
        std::stable_sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
            return std::atoi(a.c_str()) < std::atoi(b.c_str());
        });
        for (const auto& key : keys) addSeason(key, eps[key]);
    } else if (eps.is_array()) {
        for (size_t i = 0; i < eps.size(); i++) addSeason(std::to_string(i + 1), eps[i]);
    }
    return seasons;
}

/// Calls player_api.php and parses the JSON object it returns, off the UI thread
static void xtreamGetObject(const std::string& action, const std::vector<std::pair<std::string, std::string>>& params,
                            const ErrorCallback& error,
                            std::function<void(const nlohmann::json&, const XtreamAccount&)> done) {
    XtreamAccount account;
    if (!getXtreamAccount(account)) {
        if (error) error("Xtream Codes credentials not configured properly", -1);
        return;
    }
    xtreamApiGet(account, action, params, [account, action, error, done](cpr::Response r) {
        nlohmann::json d;
        if (!r.error && r.status_code == 200) d = nlohmann::json::parse(r.text, nullptr, false);
        if (d.is_discarded() || !d.is_object()) {
            brls::Logger::error("Xtream {}: invalid response (status {})", action, r.status_code);
            brls::sync([error, code = r.status_code]() {
                if (error) error(brls::getStr("tsvitch/xtream/bad_response"), code);
            });
            return;
        }
        done(d, account);
    });
}

void TsVitchClient::get_xtream_series_info(const std::string& seriesId,
                                           const std::function<void(LiveM3u8ListResult)>& callback,
                                           const ErrorCallback& error, const std::string& fallbackLogo) {
    if (seriesId.empty()) {
        if (error) error("Missing series id", -1);
        return;
    }
    xtreamGetObject("get_series_info", {{"series_id", seriesId}}, error,
                    [callback, fallbackLogo](const nlohmann::json& d, const XtreamAccount& account) {
                        auto episodes = std::make_shared<LiveM3u8ListResult>();
                        if (d.contains("episodes")) {
                            for (auto& season : parseXtreamSeasons(d, account, fallbackLogo))
                                for (auto& e : season.episodes) episodes->push_back(std::move(e.item));
                        }
                        brls::Logger::info("Xtream: parsed {} episodes", episodes->size());
                        brls::sync([callback, episodes]() {
                            if (callback) callback(std::move(*episodes));
                        });
                    });
}

/// Xtream sends programme titles and descriptions base64 encoded; text that does not decode to readable
/// UTF-8 is returned unchanged
static std::string decodeBase64Text(const std::string& text) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (text.empty() || text.size() % 4 != 0) return text;
    std::string out;
    unsigned int buffer = 0;
    int bits            = 0;
    for (char c : text) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) return text;
        buffer = (buffer << 6) | static_cast<unsigned int>(pos);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    // Valid UTF-8 without control characters, or it was not base64 after all
    for (size_t i = 0; i < out.size();) {
        auto c = static_cast<unsigned char>(out[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > out.size()) return text;
        if (length == 1 && c < 0x20 && c != '\n' && c != '\t' && c != '\r') return text;
        for (size_t k = 1; k < length; k++)
            if ((static_cast<unsigned char>(out[i + k]) & 0xC0) != 0x80) return text;
        i += length;
    }
    return out;
}

void TsVitchClient::get_xtream_short_epg(const std::string& streamId, int limit,
                                         const std::function<void(std::vector<XtreamEpgEntry>)>& callback,
                                         const ErrorCallback& error) {
    if (streamId.empty()) {
        if (error) error("Missing stream id", -1);
        return;
    }
    xtreamGetObject("get_short_epg", {{"stream_id", streamId}, {"limit", std::to_string(limit)}}, error,
                    [callback](const nlohmann::json& d, const XtreamAccount&) {
                        auto entries = std::make_shared<std::vector<XtreamEpgEntry>>();
                        if (d.contains("epg_listings") && d["epg_listings"].is_array()) {
                            for (const auto& e : d["epg_listings"]) {
                                if (!e.is_object()) continue;
                                XtreamEpgEntry entry;
                                entry.title       = sanitizeText(decodeBase64Text(safeGetString(e, "title")));
                                entry.description = sanitizeText(decodeBase64Text(safeGetString(e, "description")));
                                entry.start       = static_cast<int64_t>(safeGetNumber(e, "start_timestamp"));
                                entry.end         = static_cast<int64_t>(safeGetNumber(e, "stop_timestamp"));
                                if (entry.title.empty() || entry.start <= 0 || entry.end <= entry.start) continue;
                                entries->push_back(std::move(entry));
                            }
                        }
                        std::sort(entries->begin(), entries->end(),
                                  [](const XtreamEpgEntry& a, const XtreamEpgEntry& b) { return a.start < b.start; });
                        brls::sync([callback, entries]() {
                            if (callback) callback(std::move(*entries));
                        });
                    });
}

void TsVitchClient::get_xtream_account_info(const std::function<void(XtreamAccountInfo)>& callback,
                                            const ErrorCallback& error) {
    xtreamGetObject("", {}, error, [callback, error](const nlohmann::json& d, const XtreamAccount&) {
        if (!d.contains("user_info") || !d["user_info"].is_object()) {
            brls::sync([error]() {
                if (error) error(brls::getStr("tsvitch/xtream/bad_response"), 200);
            });
            return;
        }
        const auto& user = d["user_info"];
        auto info        = std::make_shared<XtreamAccountInfo>();
        info->status            = safeGetString(user, "status");
        info->expiresAt         = static_cast<int64_t>(safeGetNumber(user, "exp_date"));
        info->createdAt         = static_cast<int64_t>(safeGetNumber(user, "created_at"));
        info->activeConnections = static_cast<int>(safeGetNumber(user, "active_cons"));
        info->maxConnections    = static_cast<int>(safeGetNumber(user, "max_connections"));
        info->trial             = safeGetNumber(user, "is_trial") > 0;
        brls::sync([callback, info]() {
            if (callback) callback(*info);
        });
    });
}

void TsVitchClient::get_xtream_series_detail(const std::string& seriesId,
                                             const std::function<void(XtreamDetail)>& callback,
                                             const ErrorCallback& error) {
    if (seriesId.empty()) {
        if (error) error("Missing series id", -1);
        return;
    }
    xtreamGetObject("get_series_info", {{"series_id", seriesId}}, error,
                    [callback](const nlohmann::json& d, const XtreamAccount& account) {
                        static const nlohmann::json noInfo = nlohmann::json::object();
                        const auto& info = d.contains("info") && d["info"].is_object() ? d["info"] : noInfo;
                        auto detail      = std::make_shared<XtreamDetail>();
                        detail->title    = sanitizeText(safeGetString(info, "name"));
                        detail->plot     = sanitizeText(safeGetString(info, "plot"));
                        detail->genre    = sanitizeText(safeGetString(info, "genre"));
                        detail->cast     = sanitizeText(safeGetString(info, "cast"));
                        detail->director = sanitizeText(safeGetString(info, "director"));
                        detail->cover    = safeGetString(info, "cover");
                        detail->backdrop = firstBackdrop(info);
                        detail->rating   = static_cast<float>(safeGetNumber(info, "rating"));
                        detail->year     = yearFromText(safeGetString(info, "releaseDate"));
                        if (!detail->year) detail->year = yearFromText(safeGetString(info, "release_date"));
                        if (!detail->year) detail->year = yearFromText(detail->title);
                        if (d.contains("episodes")) detail->seasons = parseXtreamSeasons(d, account, detail->cover);
                        brls::sync([callback, detail]() {
                            if (callback) callback(std::move(*detail));
                        });
                    });
}

void TsVitchClient::get_xtream_movie_detail(const std::string& vodId,
                                            const std::function<void(XtreamDetail)>& callback,
                                            const ErrorCallback& error) {
    if (vodId.empty()) {
        if (error) error("Missing movie id", -1);
        return;
    }
    xtreamGetObject("get_vod_info", {{"vod_id", vodId}}, error,
                    [callback](const nlohmann::json& d, const XtreamAccount&) {
                        static const nlohmann::json noInfo = nlohmann::json::object();
                        const auto& info  = d.contains("info") && d["info"].is_object() ? d["info"] : noInfo;
                        const auto& movie = d.contains("movie_data") && d["movie_data"].is_object() ? d["movie_data"]
                                                                                                   : noInfo;
                        auto detail   = std::make_shared<XtreamDetail>();
                        detail->title = sanitizeText(safeGetString(info, "name"));
                        if (detail->title.empty()) detail->title = sanitizeText(safeGetString(movie, "name"));
                        detail->originalTitle = sanitizeText(safeGetString(info, "o_name"));
                        detail->plot          = sanitizeText(safeGetString(info, "plot"));
                        if (detail->plot.empty()) detail->plot = sanitizeText(safeGetString(info, "description"));
                        detail->genre = sanitizeText(safeGetString(info, "genre"));
                        detail->cast  = sanitizeText(safeGetString(info, "cast"));
                        if (detail->cast.empty()) detail->cast = sanitizeText(safeGetString(info, "actors"));
                        detail->director = sanitizeText(safeGetString(info, "director"));
                        detail->country  = sanitizeText(safeGetString(info, "country"));
                        detail->duration = safeGetString(info, "duration");
                        detail->cover    = safeGetString(info, "movie_image");
                        if (detail->cover.empty()) detail->cover = safeGetString(info, "cover_big");
                        detail->backdrop = firstBackdrop(info);
                        detail->rating   = static_cast<float>(safeGetNumber(info, "rating"));
                        detail->year     = yearFromText(safeGetString(info, "releasedate"));
                        if (!detail->year) detail->year = yearFromText(detail->title);
                        brls::sync([callback, detail]() {
                            if (callback) callback(std::move(*detail));
                        });
                    });
}

void TsVitchClient::get_live_channels(const std::function<void(LiveM3u8ListResult)>& callback,
                                     const ErrorCallback& error) {
    // Check IPTV mode and call appropriate function
    int iptvMode = ProgramConfig::instance().getIntOption(SettingItem::IPTV_MODE);
    
    if (iptvMode == 0) {
        // M3U8 Mode
        brls::Logger::debug("Using M3U8 mode for live channels");
        get_file_m3u8(callback, error);
    } else if (iptvMode == 1) {
        // Xtream Codes Mode: choose between Live TV and Movies (VOD)
        int contentType = ProgramConfig::instance().getXtreamContentType();
        if (contentType == 1) {
            brls::Logger::debug("Using Xtream mode for movies (VOD)");
            get_xtream_vod(callback, error);
        } else if (contentType == 2) {
            brls::Logger::debug("Using Xtream mode for series");
            get_xtream_series(callback, error);
        } else {
            brls::Logger::debug("Using Xtream mode for live channels");
            get_xtream_channels(callback, error);
        }
    } else {
        // Unknown mode
        brls::Logger::error("Unknown IPTV mode: {}", iptvMode);
        if (error) {
            error("Unknown IPTV mode configured", -1);
        }
    }
}

} // namespace tsvitch
