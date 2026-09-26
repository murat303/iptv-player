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