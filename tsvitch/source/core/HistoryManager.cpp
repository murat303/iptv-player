#include "core/HistoryManager.hpp"
#include "utils/config_helper.hpp"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

HistoryManager::HistoryManager(const std::filesystem::path& dataDir) : file_{dataDir / "history.json"} { load(); }

HistoryManager* HistoryManager::get() {
    const std::string path = ProgramConfig::instance().getConfigDir();
    brls::Logger::debug("HistoryManager path: {}", path);
    static HistoryManager instance(path);
    return &instance;
}

/// A series itself is not watched: its episodes go to the history
static bool isSeriesPage(const tsvitch::LiveM3u8& item) { return item.url.rfind("xtream-series://", 0) == 0; }

void HistoryManager::add(const tsvitch::LiveM3u8& channel) {
    if (isSeriesPage(channel)) return;
    // The same video only once; ids repeat across channels, movies and episodes, urls do not
    ring_.erase(
        std::remove_if(ring_.begin(), ring_.end(), [&](const tsvitch::LiveM3u8& c) { return c.url == channel.url; }),
        ring_.end());
    // Inserisci in testa
    ring_.push_front(channel);
    // Mantieni solo MAX_ITEMS elementi
    while (ring_.size() > MAX_ITEMS) ring_.pop_back();
    save();
}

std::deque<tsvitch::LiveM3u8> HistoryManager::recent(std::size_t limit) const {
    return {ring_.begin(), ring_.begin() + std::min(limit, ring_.size())};
}

void HistoryManager::clearByType(int type) {
    ring_.erase(std::remove_if(ring_.begin(), ring_.end(),
                               [type](const tsvitch::LiveM3u8& c) { return c.type == type; }),
                ring_.end());
    save();
}

void HistoryManager::remove(const std::string& url) {
    ring_.erase(std::remove_if(ring_.begin(), ring_.end(), [&](const tsvitch::LiveM3u8& c) { return c.url == url; }),
                ring_.end());
    save();
}

void HistoryManager::clearAll() {
    ring_.clear();
    save();
}

void HistoryManager::save() const {
    json j = ring_;
    std::ofstream(file_) << j.dump(2);
}

void HistoryManager::load() {
    std::ifstream in{file_};
    if (!in) return;
    json j = json::parse(in, nullptr, false);
    if (!j.is_array()) return;  // a damaged file starts an empty history
    try {
        ring_ = j.get<decltype(ring_)>();
    } catch (const std::exception& e) {
        brls::Logger::error("HistoryManager: damaged history: {}", e.what());
        ring_.clear();
        return;
    }
    // Series pages saved by older versions
    ring_.erase(std::remove_if(ring_.begin(), ring_.end(), isSeriesPage), ring_.end());
    while (ring_.size() > MAX_ITEMS) ring_.pop_back();
}
