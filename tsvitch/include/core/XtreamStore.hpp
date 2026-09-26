#pragma once

#include <cstdint>

#include "api/tsvitch/result/home_live_result.h"

/// Cache of the Xtream lists (live TV, movies, series) on the SD card, so the big lists are not
/// downloaded again on every visit. One file per content type in <config dir>/xtream/.
class XtreamStore {
public:
    /// Age after which a cached list is refreshed in the background
    static constexpr int64_t MAX_AGE_SECONDS = 24 * 60 * 60;

    /// Returns false when there is no usable cache for the content type (0 = live, 1 = movies, 2 = series)
    static bool load(int contentType, tsvitch::LiveM3u8ListResult& list, int64_t& savedAt);

    static void save(int contentType, const tsvitch::LiveM3u8ListResult& list);

    /// Removes every cached list (e.g. when the Xtream account changes)
    static void clear();

    /// Whether a list saved at savedAt is refreshed in the background (Settings > IPTV: every day, every week,
    /// or never)
    static bool isStale(int64_t savedAt);

    /// Saved time and number of items of a cached list, from the file's header only
    static bool header(int contentType, int64_t& savedAt, uint32_t& count);

    /// Bytes of the last download of a list (0: unknown). Servers that do not tell the size of a list get a
    /// progress bar against it.
    static int64_t lastDownloadSize(int contentType);
    static void rememberDownloadSize(int contentType, int64_t bytes);

    /// True when a list of the content type was saved before (without reading it)
    static bool exists(int contentType);
};
