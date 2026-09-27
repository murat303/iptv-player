#pragma once

#include <chrono>
#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>

#include "core/HistoryManager.hpp"
#include "utils/config_helper.hpp"
#include "utils/playback_position_manager.hpp"

namespace tsvitch {

/**
 * Movies and episodes marked as watched: by hand, or by the player once most of the video was played.
 * Kept in watched.json (URL -> Unix time of the mark) and read once.
 */
class WatchedManager {
public:
    /// The part of a video after which it counts as watched (Plex, Jellyfin and Kodi use 90% as well)
    static constexpr double THRESHOLD = 0.9;

    static bool isWatched(const std::string& url) { return !url.empty() && items().count(url) > 0; }

    /// forgetPosition: a video marked watched starts from the beginning next time (a mark by hand, the end of
    /// the video, its credits). The player keeps the position when most of it was played but some is left.
    static void setWatched(const std::string& url, bool watched, bool forgetPosition = true) {
        if (url.empty()) return;
        if (watched && forgetPosition) PlaybackPositionManager::clearPosition(url);
        auto& marks = items();
        if (watched == (marks.count(url) > 0)) return;
        if (watched)
            marks[url] = now();
        else
            marks.erase(url);
        write(marks);
        brls::Logger::info("Watched: {} -> {}", url, watched);
    }

private:
    static std::string path() { return ProgramConfig::instance().getConfigDir() + "/watched.json"; }

    static std::unordered_map<std::string, int64_t>& items() {
        static std::unordered_map<std::string, int64_t> marks = load();
        return marks;
    }

    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static std::unordered_map<std::string, int64_t> load() {
        std::unordered_map<std::string, int64_t> marks;
        std::ifstream file(path());
        if (!file.is_open()) {
            // First start with watched marks: episodes of the history without a saved position were played to
            // the end (earlier versions offered the next episode for them), so they start as watched
            int64_t position = 0, duration = 0;
            for (const auto& item : HistoryManager::get()->recent(1000))
                if (item.type == 2 && item.url.rfind("xtream-series://", 0) != 0 &&
                    !PlaybackPositionManager::getProgress(item.url, position, duration))
                    marks[item.url] = now();
            write(marks);
            brls::Logger::info("Watched: {} episodes of the history marked as watched", marks.size());
            return marks;
        }
        try {
            nlohmann::json data;
            file >> data;
            for (auto it = data.begin(); it != data.end(); ++it)
                if (it.value().is_number_integer()) marks[it.key()] = it.value().get<int64_t>();
        } catch (const std::exception& e) {
            brls::Logger::warning("Watched: cannot read {}: {}", path(), e.what());
        }
        return marks;
    }

    static void write(const std::unordered_map<std::string, int64_t>& marks) {
        nlohmann::json data = nlohmann::json::object();
        for (const auto& [url, time] : marks) data[url] = time;
        std::ofstream file(path(), std::ios::trunc);
        if (!file.is_open()) {
            brls::Logger::error("Watched: cannot write {}", path());
            return;
        }
        file << data.dump(1);
    }
};

}  // namespace tsvitch
