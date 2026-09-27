#include "core/Catalog.hpp"

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <chrono>

#include "core/XtreamStore.hpp"
#include "utils/config_helper.hpp"

Catalog& Catalog::instance() {
    // Never destroyed: background work may still finish while the app closes
    static auto* catalog = new Catalog();
    return *catalog;
}

std::shared_ptr<Catalog::Part> Catalog::build(const tsvitch::LiveM3u8ListResult& list, uint64_t seq, bool upgrade) {
    auto part     = std::make_shared<Part>();
    part->seq     = seq;
    part->upgrade = upgrade;
    part->items.reserve(list.size() / 2);
    // The adult check looks at category names: once per category, not once per item
    std::unordered_map<std::string, bool> adultCategory;
    for (const auto& item : list) {
        if (item.adult || item.id.empty()) continue;
        auto known = adultCategory.find(item.groupTitle);
        if (known == adultCategory.end())
            known = adultCategory.emplace(item.groupTitle, ProgramConfig::isAdultCategory(item.groupTitle)).first;
        if (known->second) continue;
        if (item.tmdb > 0) {
            auto found = part->byTmdb.find(item.tmdb);
            if (found != part->byTmdb.end()) {
                // The same movie again (another category, language or quality): the first one stays, and the id
                // of this one finds it too
                auto& count = part->versions[item.tmdb];
                count       = count == 0 ? 2 : count + 1;
                part->byId.emplace(item.id, found->second);
                continue;
            }
            part->byTmdb.emplace(item.tmdb, static_cast<uint32_t>(part->items.size()));
        }
        part->byId.emplace(item.id, static_cast<uint32_t>(part->items.size()));
        part->items.push_back(item);
    }
    part->items.shrink_to_fit();
    return part;
}

void Catalog::apply(int contentType, std::shared_ptr<Part> built) {
    auto& slot = contentType == 2 ? series : movies;
    // A list read from the SD card before a download finished must not replace the downloaded one
    if (slot && slot->seq > built->seq) return;
    slot = std::move(built);
    gen++;
}

void Catalog::ensureLoaded() {
    if (loaded || loading) return;
    loading  = true;
    auto seq = ++nextSeq;
    // A list downloaded in the meantime is already here and newer than the file
    bool need[2] = {movies == nullptr, series == nullptr};
    brls::Threading::async([this, seq, need]() {
        auto started = std::chrono::steady_clock::now();
        std::shared_ptr<Part> parts[2];
        for (int type = 1; type <= 2; type++) {
            if (!need[type - 1]) continue;
            tsvitch::LiveM3u8ListResult list;
            int64_t savedAt = 0;
            if (!XtreamStore::load(type, list, savedAt)) list.clear();
            parts[type - 1] = build(list, seq, XtreamStore::needsUpgrade(type));
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        brls::Logger::info("Catalog: {} movies, {} series read in {} ms", parts[0] ? parts[0]->items.size() : 0,
                           parts[1] ? parts[1]->items.size() : 0, ms.count());
        brls::sync([this, parts]() {
            if (parts[0]) this->apply(1, parts[0]);
            if (parts[1]) this->apply(2, parts[1]);
            loading = false;
            loaded  = true;
            changed.fire();
        });
    });
}

void Catalog::update(int contentType, std::shared_ptr<const tsvitch::LiveM3u8ListResult> list) {
    if ((contentType != 1 && contentType != 2) || !list) return;
    auto seq = ++nextSeq;
    brls::Threading::async([this, contentType, list, seq]() {
        auto built = build(*list, seq, false);
        brls::sync([this, contentType, built]() {
            this->apply(contentType, built);
            // Only a complete catalogue counts as read: the other list may still come from the SD card
            if (loaded) changed.fire();
        });
    });
}

void Catalog::reset() {
    // Work already started for the old lists is dropped by its older number
    nextSeq += 1000;
    auto emptyPart = [this]() {
        auto p = std::make_shared<Part>();
        p->seq = nextSeq.load();
        return p;
    };
    movies = emptyPart();
    series = emptyPart();
    gen++;
    if (loaded) changed.fire();
}

const Catalog::Part* Catalog::part(int contentType) const {
    return contentType == 2 ? series.get() : contentType == 1 ? movies.get() : nullptr;
}

const std::vector<tsvitch::LiveM3u8>& Catalog::items(int contentType) const {
    static const std::vector<tsvitch::LiveM3u8> empty;
    const Part* p = part(contentType);
    return p ? p->items : empty;
}

const tsvitch::LiveM3u8* Catalog::find(int contentType, int tmdb) const {
    const Part* p = part(contentType);
    if (!p || tmdb <= 0) return nullptr;
    auto it = p->byTmdb.find(tmdb);
    return it != p->byTmdb.end() ? &p->items[it->second] : nullptr;
}

const tsvitch::LiveM3u8* Catalog::findById(int contentType, const std::string& id) const {
    const Part* p = part(contentType);
    if (!p || id.empty()) return nullptr;
    auto it = p->byId.find(id);
    return it != p->byId.end() ? &p->items[it->second] : nullptr;
}

int Catalog::tmdbOf(const tsvitch::LiveM3u8& item, int& type) const {
    static const std::string seriesScheme = "xtream-series://";
    if (item.type == 1) {
        type = 1;
        if (item.tmdb > 0) return item.tmdb;
        const auto* found = findById(1, item.id);
        return found ? found->tmdb : 0;
    }
    if (item.type != 2) return 0;
    type = 2;
    if (item.url.rfind(seriesScheme, 0) == 0) {
        if (item.tmdb > 0) return item.tmdb;
        const auto* found = findById(2, item.id);
        return found ? found->tmdb : 0;
    }
    // An episode: its series (entries saved before 1.2 do not know it)
    const auto* series = findById(2, item.seriesId);
    return series ? series->tmdb : 0;
}

int Catalog::versions(int contentType, int tmdb) const {
    const Part* p = part(contentType);
    if (!p) return 0;
    auto it = p->versions.find(tmdb);
    return it != p->versions.end() ? it->second : (p->byTmdb.count(tmdb) ? 1 : 0);
}

bool Catalog::needsUpgrade(int contentType) const {
    const Part* p = part(contentType);
    return p && p->upgrade;
}
