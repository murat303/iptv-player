#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <borealis/core/event.hpp>

#include "api/tsvitch/result/home_live_result.h"

/// The movies and series of the saved Xtream lists, for the discovery screen and the genre pages: no adult
/// content, and one item per TMDB id (a movie that is in several categories, dubbed and subtitled, shows once).
/// It is built off the UI thread from the lists on the SD card, or from a list that was just downloaded, and read
/// on the UI thread only.
class Catalog {
public:
    static Catalog& instance();

    /// Reads the saved lists once, in the background; the changed event fires when they are in memory. UI thread.
    void ensureLoaded();

    /// A list was downloaded again: its items replace the old ones. Any thread.
    void update(int contentType, std::shared_ptr<const tsvitch::LiveM3u8ListResult> list);

    /// The account changed: the lists are gone. UI thread.
    void reset();

    /// Both lists were read (they may be empty: never downloaded)
    bool isLoaded() const { return loaded; }

    /// 1 = movies, 2 = series
    const std::vector<tsvitch::LiveM3u8>& items(int contentType) const;

    const tsvitch::LiveM3u8* find(int contentType, int tmdb) const;

    const tsvitch::LiveM3u8* findSeries(const std::string& seriesId) const { return findById(2, seriesId); }

    /// By the provider's id (stream_id of a movie, series_id of a series)
    const tsvitch::LiveM3u8* findById(int contentType, const std::string& id) const;

    /// The TMDB id of a movie or series item, also of an old history/favorite entry saved without it; an episode
    /// gives its series' id (type is set to 1 or 2). 0 when unknown.
    int tmdbOf(const tsvitch::LiveM3u8& item, int& type) const;

    /// How many items of the provider's list have this TMDB id (dubbed, subtitled, 4K...)
    int versions(int contentType, int tmdb) const;

    /// The saved list comes from an older version and has no TMDB ids yet
    bool needsUpgrade(int contentType) const;

    /// Grows whenever the items change (screens rebuild what they show)
    uint64_t generation() const { return gen; }

    /// Fires on the UI thread when a list was read or replaced
    brls::Event<>* getChangedEvent() { return &changed; }

private:
    struct Part {
        std::vector<tsvitch::LiveM3u8> items;
        std::unordered_map<int, uint32_t> byTmdb;
        std::unordered_map<std::string, uint32_t> byId;
        std::unordered_map<int, uint16_t> versions;  // only TMDB ids found more than once
        bool upgrade = false;
        uint64_t seq = 0;
    };

    static std::shared_ptr<Part> build(const tsvitch::LiveM3u8ListResult& list, uint64_t seq, bool upgrade);
    const Part* part(int contentType) const;
    void apply(int contentType, std::shared_ptr<Part> built);

    std::shared_ptr<Part> movies, series;
    bool loaded        = false;
    bool loading       = false;
    uint64_t gen       = 0;
    std::atomic<uint64_t> nextSeq{0};
    brls::Event<> changed;
};
