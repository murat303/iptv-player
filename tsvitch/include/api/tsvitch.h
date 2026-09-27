#pragma once

#include <string>
#include <map>
#include <vector>
#include <future>

namespace tsvitch {

class LiveM3u8;
struct XtreamDetail;
struct XtreamAccountInfo;
struct XtreamEpgEntry;
typedef std::vector<LiveM3u8> LiveM3u8ListResult;

using ErrorCallback = std::function<void(const std::string&, int code)>;

/// Progress of a list download (live channels, movies or series), delivered on the UI thread
struct XtreamLoadState {
    enum Phase { QUEUED, CATEGORIES, DOWNLOADING, RETRY, PREPARING };
    int contentType    = 0;
    Phase phase        = QUEUED;
    int64_t bytes      = 0;  // DOWNLOADING: received so far
    int64_t total      = 0;  // DOWNLOADING: 0 when the server does not tell the size
    int attempt        = 0;  // RETRY: this retry and how many there can be
    int attempts       = 0;
    int retryInSeconds = 0;
};

/// One call to the provider's player_api.php, for the connection test (no address and no account in it)
struct XtreamRequestLog {
    std::string action;     // player_api action ("" is the account)
    int64_t time   = 0;     // Unix time it was answered (or given up)
    int waitMs     = 0;     // how long it waited in the app first (another request of its kind, the gap between them)
    int durationMs = 0;     // from the first try to the answer, retries included
    int attempts   = 0;
    int status     = 0;     // HTTP status of the last try (0: no answer)
    int error      = 0;     // cpr::ErrorCode of the last try (0: none, -1: no Xtream account)
    bool cut       = false; // a 200 answer that stopped in the middle
    size_t bytes   = 0;
};

#define CLIENT tsvitch::TsVitchClient
#define CLIENT_ERR const std::string &error, int code

class TsVitchClient {
public:
    static void get_file_m3u8(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                              const ErrorCallback& error                              = nullptr);

    static void get_xtream_channels(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                                   const ErrorCallback& error                               = nullptr);

    static void get_xtream_channels_with_retry(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                                              const ErrorCallback& error                               = nullptr,
                                              int maxRetries                                           = 3);

    static void get_xtream_vod(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                               const ErrorCallback& error                              = nullptr);

    static void get_xtream_series(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                                  const ErrorCallback& error                              = nullptr);

    // Lista os nomes das categorias de um tipo (0=Live, 1=Movies, 2=Series)
    static void get_xtream_category_names(int contentType,
                                          const std::function<void(std::vector<std::string>)>& callback = nullptr,
                                          const ErrorCallback& error                                     = nullptr);

    // fallbackLogo: picture for episodes without their own still (usually the series cover)
    static void get_xtream_series_info(const std::string& seriesId,
                                       const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                                       const ErrorCallback& error                              = nullptr,
                                       const std::string& fallbackLogo                         = "");

    // Details of a movie: plot, cast, genre, duration, pictures (action=get_vod_info)
    static void get_xtream_movie_detail(const std::string& vodId,
                                        const std::function<void(XtreamDetail)>& callback = nullptr,
                                        const ErrorCallback& error                       = nullptr);

    // Called when the app closes: Xtream requests still waiting or retrying give up at once
    static void stopRequests();

    // The screen that shows list downloads; nullptr removes it
    static void setXtreamLoadObserver(std::function<void(const XtreamLoadState&)> observer);

    // The last requests to the provider, newest first (at most 20)
    static std::vector<XtreamRequestLog> recentXtreamRequests();

    // One small request for the connection test (the lane of small requests); done gets its record on the UI thread
    static void testXtreamRequest(const std::string& action,
                                  const std::vector<std::pair<std::string, std::string>>& params,
                                  const std::function<void(XtreamRequestLog)>& done);

    // Status, end date and connections of the subscription (player_api.php without an action)
    static void get_xtream_account_info(const std::function<void(XtreamAccountInfo)>& callback = nullptr,
                                        const ErrorCallback& error                            = nullptr);

    // The current and next programmes of a live channel, oldest first (action=get_short_epg)
    static void get_xtream_short_epg(const std::string& streamId, int limit,
                                     const std::function<void(std::vector<XtreamEpgEntry>)>& callback = nullptr,
                                     const ErrorCallback& error                                   = nullptr);

    // Details, seasons and episodes of a series (action=get_series_info)
    static void get_xtream_series_detail(const std::string& seriesId,
                                         const std::function<void(XtreamDetail)>& callback = nullptr,
                                         const ErrorCallback& error                       = nullptr);

    static void get_live_channels(const std::function<void(LiveM3u8ListResult)>& callback = nullptr,
                                 const ErrorCallback& error                               = nullptr);

    static void register_user(
                              const std::function<void(const std::string&, int)>& callback = nullptr,
                              const ErrorCallback& error                                   = nullptr);

    static void check_user_id(
                              const std::function<void(const std::string&, int)>& callback = nullptr,
                              const ErrorCallback& error                                   = nullptr);

    static void get_ad(
                       const std::function<void(const std::string&, int)>& callback = nullptr,
                       const ErrorCallback& error                                   = nullptr);
};
}  // namespace tsvitch