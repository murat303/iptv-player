#pragma once

#include <string>
#include <vector>

#include "home_live_result.h"

namespace tsvitch {

/// An episode of a series with what its row on the detail screen shows
struct XtreamEpisode {
    LiveM3u8 item;         // playable item: url, still, title
    int number = 0;        // episode number inside the season
    std::string plot;
    std::string duration;  // as sent by the server, e.g. "00:44:27"
    std::string airDate;
};

struct XtreamSeason {
    int number = 0;
    std::string name;
    std::vector<XtreamEpisode> episodes;
};

/// Everything the detail screen shows for a movie or a series
struct XtreamDetail {
    std::string title;
    std::string originalTitle;
    std::string plot;
    std::string genre;
    std::string cast;
    std::string director;
    std::string country;
    std::string duration;  // movies, e.g. "02:15:08"
    std::string cover;     // poster
    std::string backdrop;  // wide background picture
    std::string trailer;   // YouTube id the provider lists
    int year     = 0;
    float rating = 0;      // 0-10, 0 = unknown
    std::vector<XtreamSeason> seasons;  // series only
};

/// A programme of a live channel
struct XtreamEpgEntry {
    std::string title;
    std::string description;
    int64_t start = 0;  // Unix time
    int64_t end   = 0;
};

/// The subscription as the server reports it (player_api.php without an action)
struct XtreamAccountInfo {
    std::string status;         // "Active", "Expired", "Banned", "Disabled"...
    int64_t expiresAt   = 0;    // Unix time, 0 = no end date
    int64_t createdAt   = 0;    // Unix time, 0 = unknown
    int activeConnections = 0;
    int maxConnections    = 0;
    bool trial            = false;
};

}  // namespace tsvitch
