#include "core/FavoriteManager.hpp"
#include "core/Catalog.hpp"
#include "utils/config_helper.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

FavoriteManager::FavoriteManager(const std::filesystem::path& dataDir)
    : file_{dataDir / "favorites.json"} {
    load();
}

FavoriteManager* FavoriteManager::get() {
    const std::string path = ProgramConfig::instance().getConfigDir();
    static FavoriteManager instance(path);
    return &instance;
}

tsvitch::LiveM3u8 FavoriteManager::target(const tsvitch::LiveM3u8& item) {
    bool episode = item.type == 2 && item.url.rfind("xtream-series://", 0) != 0;
    if (episode)
        if (const auto* series = Catalog::instance().seriesOf(item)) return *series;
    return item;
}

void FavoriteManager::toggle(const tsvitch::LiveM3u8& item) {
    const tsvitch::LiveM3u8 channel = target(item);
    auto it = std::find_if(set_.begin(), set_.end(),
        [&](const tsvitch::LiveM3u8& c){ return c.url == channel.url; });
    if (it != set_.end()) {
        set_.erase(it);
        urlCache_.erase(channel.url);
    } else {
        set_.push_back(channel);
        urlCache_.insert(channel.url);
    }
    save();
    GA("toggle_favorite", {
        {"title", channel.title},
        {"url", channel.url},
        {"group_title", channel.groupTitle},
        {"is_favorite", isFavorite(channel.url) ? "true" : "false"}
    });
}


bool FavoriteManager::isFavorite(const std::string& url ) const {
    return urlCache_.count(url) > 0;
}

bool FavoriteManager::isFavorite(const tsvitch::LiveM3u8& item) const { return isFavorite(target(item).url); }

void FavoriteManager::convertEpisodes() {
    bool changed = false;
    for (size_t i = 0; i < set_.size();) {
        tsvitch::LiveM3u8 series = target(set_[i]);
        if (series.url == set_[i].url) {
            i++;
            continue;
        }
        urlCache_.erase(set_[i].url);
        changed = true;
        // The series may be a favorite already
        if (urlCache_.count(series.url)) {
            set_.erase(set_.begin() + static_cast<long>(i));
            continue;
        }
        set_[i] = series;
        urlCache_.insert(series.url);
        i++;
    }
    if (changed) save();
}

void FavoriteManager::save() const {
    json j = set_;
    std::ofstream(file_) << j.dump(2);
}

void FavoriteManager::load() {
    if (std::ifstream in{file_}; in) {
        json j; in >> j;
        set_ = j.get<decltype(set_)>();
        
        // Popola la cache urlCache_ per O(1) lookups
        urlCache_.clear();
        for (const auto& channel : set_) {
            urlCache_.insert(channel.url);
        }
    }
}
std::vector<tsvitch::LiveM3u8> FavoriteManager::getFavorites() const {
    return std::vector<tsvitch::LiveM3u8>(set_.begin(), set_.end());
}
