#pragma once
#include <deque>
#include <unordered_set>
#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>
#include "api/tsvitch/result/home_live_result.h"  

class FavoriteManager {
public:
    explicit FavoriteManager(const std::filesystem::path& dataDir);

    /// What a favorite of item is: the item itself, or for an episode its series (favorites keep series, not
    /// episodes). An episode whose series the catalogue does not know stays itself.
    static tsvitch::LiveM3u8 target(const tsvitch::LiveM3u8& item);

    /// Adds or removes the favorite of item (of an episode: its series)
    void toggle(const tsvitch::LiveM3u8& channel);
    bool isFavorite(const std::string& url ) const;
    /// Whether item (an episode: its series) is a favorite
    bool isFavorite(const tsvitch::LiveM3u8& item) const;

    /// Episodes saved by an earlier version become their series, once the catalogue knows them
    void convertEpisodes();

    std::vector<tsvitch::LiveM3u8> getFavorites() const;

    void save() const;
    void load();

    //get istantance
    static FavoriteManager* get();

private:
    std::filesystem::path file_;
    std::deque<tsvitch::LiveM3u8> set_;
    std::unordered_set<std::string> urlCache_; // Cache O(1) per isFavorite
};
