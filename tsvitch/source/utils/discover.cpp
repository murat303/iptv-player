#include "utils/discover.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>

#include "core/Catalog.hpp"
#include "core/FavoriteManager.hpp"
#include "core/HistoryManager.hpp"
#include "core/TmdbStore.hpp"
#include "utils/config_helper.hpp"
#include "utils/genres.hpp"
#include "utils/image_helper.hpp"
#include "utils/playback_position_manager.hpp"
#include "utils/watched_manager.hpp"

using namespace brls::literals;

namespace tsvitch::discover {

namespace {

const std::string SERIES_SCHEME = "xtream-series://";
constexpr size_t SHELF_SIZE     = 20;  // titles on a poster shelf
constexpr size_t COVER_ITEMS    = 60;  // titles kept for a cover (its page computes all of them)

/// A visible title of the catalogue with its TMDB data (valid inside TmdbStore::read only)
struct Entry {
    const LiveM3u8* item = nullptr;
    const TmdbMeta* meta = nullptr;
    int type             = 1;

    uint32_t genres() const { return meta && meta->genres ? meta->genres : item->genres; }
    int year() const { return item->year ? item->year : meta ? meta->year : 0; }
    float popularity() const { return meta ? meta->popularity : 0; }
    int votes() const { return meta ? meta->votes : 0; }
    float vote() const { return meta && meta->votes > 0 ? meta->vote : item->rating; }
    bool hasKeyword(std::initializer_list<int32_t> ids) const {
        if (!meta) return false;
        for (auto id : ids)
            if (std::find(meta->keywords.begin(), meta->keywords.end(), id) != meta->keywords.end()) return true;
        return false;
    }
    bool hasCompany(std::initializer_list<int32_t> ids) const {
        if (!meta) return false;
        for (auto id : ids)
            if (std::find(meta->companies.begin(), meta->companies.end(), id) != meta->companies.end()) return true;
        return false;
    }
    bool lang(const char* code) const { return meta && meta->lang[0] == code[0] && meta->lang[1] == code[1]; }
};

enum class Sort { POPULAR, VOTE, REVENUE, YEAR_ASC, ADDED };

/// A collection whose titles a rule picks out of the catalogue
struct Def {
    std::string id;
    std::string title;
    int type = 0;  // 1 movies, 2 series, 0 both
    std::function<bool(const Entry&)> match;
    Sort sort         = Sort::POPULAR;
    size_t minimum    = 6;  // fewer titles: no cover
    uint32_t colorA   = 0;
    uint32_t colorB   = 0;
};

// Cover colours: a pair per collection, from its id so it stays the same
const std::pair<uint32_t, uint32_t> PALETTE[] = {
    {0xE65100, 0x4A148C}, {0x00897B, 0x1A237E}, {0xC2185B, 0x311B92}, {0x1565C0, 0x00251A},
    {0xF9A825, 0xBF360C}, {0x6A1B9A, 0x0D47A1}, {0x2E7D32, 0x1B2631}, {0xAD1457, 0x3E2723},
    {0x0277BD, 0x4A148C}, {0xD84315, 0x263238}, {0x00838F, 0x283593}, {0x8E24AA, 0xBF360C},
};

void paint(Collection& c) {
    size_t hash = std::hash<std::string>{}(c.id);
    const auto& pair = PALETTE[hash % (sizeof(PALETTE) / sizeof(PALETTE[0]))];
    c.colorA = pair.first;
    c.colorB = pair.second;
}

float quality(const Entry& e) {
    // A good vote counts when many people voted; the provider's rating (without a count) counts a little
    float votes = static_cast<float>(e.votes());
    float trust = e.meta && e.votes() > 0 ? std::min(1.0f, votes / 500.0f) : 0.4f;
    return e.vote() * trust + std::log1p(e.popularity()) * 0.6f;
}

void sortEntries(std::vector<const Entry*>& list, Sort sort) {
    auto by = [&list](auto better) { std::stable_sort(list.begin(), list.end(), better); };
    switch (sort) {
        case Sort::POPULAR:
            by([](const Entry* a, const Entry* b) {
                if (a->popularity() != b->popularity()) return a->popularity() > b->popularity();
                return a->vote() > b->vote();
            });
            break;
        case Sort::VOTE:
            by([](const Entry* a, const Entry* b) { return quality(*a) > quality(*b); });
            break;
        case Sort::REVENUE:
            by([](const Entry* a, const Entry* b) {
                return (a->meta ? a->meta->revenue : 0) > (b->meta ? b->meta->revenue : 0);
            });
            break;
        case Sort::YEAR_ASC:
            by([](const Entry* a, const Entry* b) { return a->year() < b->year(); });
            break;
        case Sort::ADDED:
            by([](const Entry* a, const Entry* b) { return a->item->added > b->item->added; });
            break;
    }
}

std::string themeTitle(const std::string& id) { return brls::getStr("tsvitch/discover/theme/" + id); }

/// The themes (Chillio-like curated lists) and what picks their titles
const std::vector<Def>& themeDefs() {
    static const std::vector<Def> defs = [] {
        using G = uint32_t;
        auto theme = [](const char* id, int type, std::function<bool(const Entry&)> match, Sort sort) {
            Def d;
            d.id    = std::string("theme:") + id;
            d.type  = type;
            d.match = std::move(match);
            d.sort  = sort;
            return d;
        };
        std::vector<Def> list = {
            theme("blockbusters", 1, [](const Entry& e) { return e.meta && e.meta->revenue >= 100; }, Sort::REVENUE),
            theme("classics", 1, [](const Entry& e) { return e.year() > 0 && e.year() <= 1985 && e.votes() >= 800; },
                  Sort::VOTE),
            theme("mind_twists", 1,
                  [](const Entry& e) {
                      return e.votes() >= 200 && e.hasKeyword({326438, 362567, 157171, 174089, 12565});
                  },
                  Sort::VOTE),
            theme("books", 0, [](const Entry& e) { return e.hasKeyword({818}); }, Sort::POPULAR),
            theme("true_stories", 0, [](const Entry& e) { return e.hasKeyword({9672}); }, Sort::POPULAR),
            theme("epic_fantasy", 0,
                  [](const Entry& e) {
                      G g = e.genres();
                      return e.votes() >= 300 &&
                             (((g & genre::FANTASY) && (g & genre::ADVENTURE)) || e.hasKeyword({234213, 211227}));
                  },
                  Sort::POPULAR),
            theme("superheroes", 0, [](const Entry& e) { return e.hasKeyword({9715}); }, Sort::POPULAR),
            theme("space", 1, [](const Entry& e) { return e.hasKeyword({3801, 252634}); }, Sort::POPULAR),
            theme("time_travel", 0, [](const Entry& e) { return e.hasKeyword({4379}); }, Sort::POPULAR),
            theme("dystopia", 0, [](const Entry& e) { return e.hasKeyword({4565, 4458}); }, Sort::POPULAR),
            theme("heist", 1, [](const Entry& e) { return e.hasKeyword({10051}); }, Sort::POPULAR),
            theme("serial_killers", 0, [](const Entry& e) { return e.hasKeyword({10714}); }, Sort::POPULAR),
            theme("zombies", 0, [](const Entry& e) { return e.hasKeyword({12377, 186565}); }, Sort::POPULAR),
            theme("survival", 1, [](const Entry& e) { return e.hasKeyword({10349}); }, Sort::POPULAR),
            theme("ai", 0, [](const Entry& e) { return e.hasKeyword({310}); }, Sort::POPULAR),
            theme("martial_arts", 1, [](const Entry& e) { return e.hasKeyword({779}); }, Sort::POPULAR),
            theme("revenge", 1, [](const Entry& e) { return e.hasKeyword({9748}); }, Sort::POPULAR),
            theme("coming_of_age", 0, [](const Entry& e) { return e.hasKeyword({10683}); }, Sort::POPULAR),
            theme("sports", 0, [](const Entry& e) { return e.hasKeyword({6075}); }, Sort::POPULAR),
            theme("biographies", 1, [](const Entry& e) { return e.hasKeyword({5565}); }, Sort::POPULAR),
            theme("christmas", 1, [](const Entry& e) { return e.hasKeyword({207317}); }, Sort::POPULAR),
            theme("eye_openers", 0,
                  [](const Entry& e) {
                      return (e.genres() & genre::DOCUMENTARY) && e.meta && e.meta->vote >= 7.0f && e.votes() >= 50;
                  },
                  Sort::VOTE),
            theme("true_crime", 0,
                  [](const Entry& e) {
                      G g = e.genres();
                      return e.hasKeyword({33722}) || ((g & genre::DOCUMENTARY) && (g & genre::CRIME));
                  },
                  Sort::POPULAR),
            theme("family_animation", 1,
                  [](const Entry& e) {
                      G g = e.genres();
                      return (g & genre::ANIMATION) && (g & genre::FAMILY) && e.votes() >= 100;
                  },
                  Sort::POPULAR),
            theme("hidden_gems", 1,
                  [](const Entry& e) {
                      return e.meta && e.meta->vote >= 7.3f && e.votes() >= 100 && e.votes() <= 1500;
                  },
                  Sort::VOTE),
            theme("miniseries", 2, [](const Entry& e) { return e.meta && e.meta->seasons == 1 && e.meta->status == 1; },
                  Sort::POPULAR),
            theme("completed", 2, [](const Entry& e) { return e.meta && e.meta->status == 1 && e.votes() >= 200; },
                  Sort::VOTE),
        };
        return list;
    }();
    return defs;
}

/// Studios (movies) and platforms (series) chosen by hand; other networks come from the data
struct Studio {
    const char* id;
    const char* name;
    int type;
    std::vector<int32_t> ids;
};

const std::vector<Studio>& studios() {
    static const std::vector<Studio> list = {
        {"pixar", "Pixar", 1, {3}},
        {"disney", "Disney", 1, {2, 6125, 158526}},
        {"marvel", "Marvel", 1, {420}},
        {"dc", "DC", 1, {128064, 9993, 10576}},
        {"ghibli", "Studio Ghibli", 1, {10342}},
        {"dreamworks", "DreamWorks", 1, {521}},
        {"illumination", "Illumination", 1, {6704}},
        {"lucasfilm", "Lucasfilm", 1, {1}},
        {"a24", "A24", 1, {41077, 293354}},
        {"blumhouse", "Blumhouse", 1, {3172}},
        {"netflix", "Netflix", 2, {213}},
        {"hbo", "HBO", 2, {49, 3186}},
        {"disney_plus", "Disney+", 2, {2739}},
        {"apple_tv", "Apple TV+", 2, {2552}},
        {"prime_video", "Prime Video", 2, {1024}},
        {"crunchyroll", "Crunchyroll", 2, {1112}},
    };
    return list;
}

Def studioDef(const Studio& studio) {
    Def d;
    d.id    = std::string("studio:") + studio.id;
    d.type  = studio.type;
    d.title = studio.type == 2 ? brls::getStr("tsvitch/discover/network", studio.name) : std::string(studio.name);
    std::vector<int32_t> ids = studio.ids;
    d.match = [ids](const Entry& e) {
        if (!e.meta) return false;
        for (auto id : ids)
            if (std::find(e.meta->companies.begin(), e.meta->companies.end(), id) != e.meta->companies.end())
                return true;
        return false;
    };
    return d;
}

Def networkDef(int network, const std::string& name) {
    Def d;
    d.id    = "network:" + std::to_string(network);
    d.type  = 2;
    d.title = brls::getStr("tsvitch/discover/network", name.empty() ? std::to_string(network) : name);
    d.match = [network](const Entry& e) {
        return e.meta && std::find(e.meta->companies.begin(), e.meta->companies.end(), network) !=
                             e.meta->companies.end();
    };
    d.minimum = 8;
    return d;
}

const std::vector<Def>& worldDefs() {
    static const std::vector<Def> defs = [] {
        auto language = [](const char* id, const char* code) {
            Def d;
            d.id           = std::string("lang:") + id;
            std::string lc = code;
            d.match        = [lc](const Entry& e) { return e.lang(lc.c_str()); };
            d.minimum      = 8;
            return d;
        };
        std::vector<Def> list = {language("turkish", "tr"), language("korean", "ko")};
        Def anime;
        anime.id    = "lang:anime";
        anime.match = [](const Entry& e) {
            return (e.lang("ja") && (e.genres() & genre::ANIMATION)) || e.hasKeyword({210024});
        };
        list.push_back(anime);
        for (auto [id, code] : std::vector<std::pair<const char*, const char*>>{{"indian", "hi"},
                                                                                {"spanish", "es"},
                                                                                {"french", "fr"},
                                                                                {"italian", "it"},
                                                                                {"german", "de"},
                                                                                {"japanese", "ja"},
                                                                                {"chinese", "zh"},
                                                                                {"nordic", "sv"}})
            list.push_back(language(id, code));
        return list;
    }();
    return defs;
}

const std::vector<Def>& decadeDefs() {
    static const std::vector<Def> defs = [] {
        std::vector<Def> list;
        for (int decade = 2010; decade >= 1960; decade -= 10) {
            Def d;
            d.id    = "decade:" + std::to_string(decade);
            d.type  = 1;
            d.match = [decade](const Entry& e) {
                return e.year() >= decade && e.year() < decade + 10 && (e.votes() >= 200 || (!e.meta && e.vote() > 0));
            };
            d.minimum = 8;
            list.push_back(std::move(d));
        }
        return list;
    }();
    return defs;
}

std::string defTitle(const Def& def) {
    if (!def.title.empty()) return def.title;
    auto colon = def.id.find(':');
    std::string kind = def.id.substr(0, colon), name = def.id.substr(colon + 1);
    if (kind == "theme") return themeTitle(name);
    if (kind == "lang") return brls::getStr("tsvitch/discover/lang/" + name);
    if (kind == "decade") return brls::getStr("tsvitch/discover/decade/" + name);
    return name;
}

struct Award {
    std::string id;
    int type = 1;
    std::vector<int> tmdb;  // newest award first
};

const std::vector<Award>& awards() {
    static const std::vector<Award> list = [] {
        std::vector<Award> out;
        std::ifstream in(std::string(BRLS_RESOURCES) + "discover/awards.json");
        if (!in) return out;
        auto json = nlohmann::json::parse(in, nullptr, false);
        if (!json.is_object() || !json.contains("awards") || !json["awards"].is_array()) return out;
        for (const auto& entry : json["awards"]) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) continue;
            Award award;
            award.id   = entry["id"].get<std::string>();
            award.type = entry.value("type", 1);
            if (entry.contains("items") && entry["items"].is_array())
                for (const auto& item : entry["items"])
                    if (item.is_array() && !item.empty() && item[0].is_number_integer())
                        award.tmdb.push_back(item[0].get<int>());
            out.push_back(std::move(award));
        }
        return out;
    }();
    return list;
}

std::unordered_set<std::string> lockedCategories() {
    std::unordered_set<std::string> names, locked;
    for (int type : {1, 2})
        for (const auto& item : Catalog::instance().items(type)) names.insert(item.groupTitle);
    for (const auto& name : names)
        if (ProgramConfig::instance().isCategoryLocked(name)) locked.insert(name);
    return locked;
}

/// The catalogue's titles that may be shown, with their TMDB data (inside TmdbStore::read)
std::vector<Entry> entries(const TmdbStore::Map& map, const std::unordered_set<std::string>& locked) {
    std::vector<Entry> out;
    for (int type : {1, 2}) {
        const auto& items = Catalog::instance().items(type);
        out.reserve(out.size() + items.size());
        for (const auto& item : items) {
            if (!locked.empty() && locked.count(item.groupTitle)) continue;
            Entry e;
            e.item = &item;
            e.type = type;
            if (item.tmdb > 0) {
                auto it = map.find(TmdbStore::key(type, item.tmdb));
                if (it != map.end() && !it->second.missing) e.meta = &it->second;
            }
            out.push_back(e);
        }
    }
    return out;
}

/// What was watched: movies by their url, series by an episode in the history
struct Seen {
    std::unordered_set<std::string> urls;
    std::unordered_set<std::string> series;
    bool has(const Entry& e) const {
        if (e.type == 2) return series.count(e.item->id) > 0;
        return urls.count(e.item->url) > 0 || WatchedManager::isWatched(e.item->url);
    }
};

Seen seenTitles() {
    Seen seen;
    for (const auto& item : HistoryManager::get()->recent(100)) {
        if (item.type == 1) seen.urls.insert(item.url);
        if (item.type == 2 && !item.seriesId.empty()) seen.series.insert(item.seriesId);
    }
    return seen;
}

/// Picks and sorts the titles of a rule; keeps at most max of them (0: all) and counts all
Collection runDef(const Def& def, const std::vector<Entry>& all, size_t max) {
    Collection c;
    c.id    = def.id;
    c.title = defTitle(def);
    c.type  = def.type;
    std::vector<const Entry*> picked;
    for (const auto& e : all)
        if ((def.type == 0 || def.type == e.type) && def.match(e)) picked.push_back(&e);
    sortEntries(picked, def.sort);
    c.count = picked.size();
    size_t keep = max == 0 ? picked.size() : std::min(max, picked.size());
    c.items.reserve(keep);
    for (size_t i = 0; i < keep; i++) c.items.push_back(*picked[i]->item);
    if (def.colorA || def.colorB) {
        c.colorA = def.colorA;
        c.colorB = def.colorB;
    } else {
        paint(c);
    }
    return c;
}

Collection genreCollectionOf(int type, uint32_t bit, const std::vector<Entry>& all, size_t max) {
    Def def;
    const genre::Info* info = genre::find(bit);
    def.id     = std::string("genre:") + std::to_string(type) + ":" + (info ? info->key : "");
    def.title  = genre::name(bit);
    def.type   = type;
    def.match  = [bit](const Entry& e) { return (e.genres() & bit) != 0; };
    def.sort   = Sort::POPULAR;
    def.colorA = info ? info->colorA : 0x455A64;
    def.colorB = info ? info->colorB : 0x263238;
    return runDef(def, all, max);
}

Collection awardCollectionOf(const Award& award, const std::vector<Entry>& all,
                             const std::unordered_map<int64_t, const Entry*>& byRef, size_t max) {
    Collection c;
    c.id    = "award:" + award.id;
    c.title = brls::getStr("tsvitch/discover/award/" + award.id);
    c.type  = award.type;
    std::unordered_set<int> added;
    for (int id : award.tmdb) {
        auto it = byRef.find(TmdbStore::key(award.type, id));
        if (it == byRef.end() || !added.insert(id).second) continue;
        c.count++;
        if (max == 0 || c.items.size() < max) c.items.push_back(*it->second->item);
    }
    paint(c);
    return c;
}

std::unordered_map<int64_t, const Entry*> indexByRef(const std::vector<Entry>& all) {
    std::unordered_map<int64_t, const Entry*> byRef;
    byRef.reserve(all.size());
    for (const auto& e : all)
        if (e.item->tmdb > 0) byRef.emplace(TmdbStore::key(e.type, e.item->tmdb), &e);
    return byRef;
}

std::string heroMeta(const Entry& e) {
    std::vector<std::string> parts;
    if (e.vote() > 0) parts.push_back(fmt::format("★ {:.1f}", e.vote()));
    if (e.year() > 0) parts.push_back(std::to_string(e.year()));
    std::string names;
    int count = 0;
    for (uint32_t bit : genre::split(e.genres())) {
        if (count++ == 3) break;
        if (!names.empty()) names += ", ";
        names += genre::name(bit);
    }
    if (!names.empty()) parts.push_back(names);
    std::string out;
    for (const auto& part : parts) out += (out.empty() ? "" : "  ·  ") + part;
    return out;
}

std::string backdropUrl(const Entry& e) {
    return e.meta && !e.meta->backdrop.empty() ? "https://image.tmdb.org/t/p/w1280" + e.meta->backdrop : "";
}

std::vector<std::string> hiddenShelves() {
    std::vector<std::string> out;
    std::string text = ProgramConfig::instance().getSettingItem(SettingItem::DISCOVER_HIDDEN, std::string{});
    size_t start     = 0;
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

}  // namespace

const std::vector<std::pair<std::string, std::string>>& shelfList() {
    static const std::vector<std::pair<std::string, std::string>> list = {
        {"hero", "tsvitch/discover/shelf/hero"},
        {"continue", "tsvitch/discover/shelf/continue"},
        {"trending", "tsvitch/discover/shelf/trending"},
        {"for_you", "tsvitch/discover/shelf/for_you"},
        {"because", "tsvitch/discover/shelf/because_setting"},
        {"popular_movies", "tsvitch/discover/shelf/popular_movies"},
        {"popular_series", "tsvitch/discover/shelf/popular_series"},
        {"new_movies", "tsvitch/discover/shelf/new_movies"},
        {"new_series", "tsvitch/discover/shelf/new_series"},
        {"movie_genres", "tsvitch/discover/shelf/movie_genres"},
        {"series_genres", "tsvitch/discover/shelf/series_genres"},
        {"themes", "tsvitch/discover/shelf/themes"},
        {"favorite_genre", "tsvitch/discover/shelf/favorite_genre_setting"},
        {"top_movies", "tsvitch/discover/shelf/top_movies"},
        {"top_series", "tsvitch/discover/shelf/top_series"},
        {"studios", "tsvitch/discover/shelf/studios"},
        {"world", "tsvitch/discover/shelf/world"},
        {"awards", "tsvitch/discover/shelf/awards"},
        {"decades", "tsvitch/discover/shelf/decades"},
        {"franchises", "tsvitch/discover/shelf/franchises"},
    };
    return list;
}

bool isHidden(const std::string& shelfId) {
    auto hidden = hiddenShelves();
    return std::find(hidden.begin(), hidden.end(), shelfId) != hidden.end();
}

void setHidden(const std::string& shelfId, bool hide) {
    auto hidden = hiddenShelves();
    hidden.erase(std::remove(hidden.begin(), hidden.end(), shelfId), hidden.end());
    if (hide) hidden.push_back(shelfId);
    std::string text;
    for (const auto& id : hidden) text += (text.empty() ? "" : ",") + id;
    ProgramConfig::instance().setSettingItem(SettingItem::DISCOVER_HIDDEN, text);
}

std::string posterOf(const LiveM3u8& item) { return ImageHelper::smallPoster(item.logo); }

std::string cleanTitle(const std::string& title) {
    // Tags providers add at the end of a name (compared in capitals; the Turkish ones also with their dotted I)
    static const std::vector<std::string> tags = {"TR YERLI", "TR YERL\xC4\xB0", "YERLI", "YERL\xC4\xB0", "TR", "4K HDR",
                                                  "4K", "HDR", "UHD", "FHD", "HD", "DUBLAJ", "ALTYAZILI", "MULTI",
                                                  "TABII", "TABİİ", "EXXEN", "BLUTV", "GAIN"};
    auto upperTail = [](const std::string& text, size_t size) {
        std::string tail = text.substr(text.size() - size);
        for (auto& c : tail)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return tail;
    };
    auto isYear = [](const std::string& text, size_t at) {
        if (at + 4 > text.size()) return false;
        for (size_t i = at; i < at + 4; i++)
            if (text[i] < '0' || text[i] > '9') return false;
        return text.compare(at, 2, "19") == 0 || text.compare(at, 2, "20") == 0;
    };
    std::string t = title;
    for (bool changed = true; changed && t.size() > 1;) {
        changed = false;
        while (!t.empty() && (t.back() == ' ' || t.back() == '-' || t.back() == '|' || t.back() == ':' ||
                              t.back() == '.' || t.back() == ',')) {
            t.pop_back();
            changed = true;
        }
        // Stars some providers put after new titles ("⭐⭐", with or without the emoji selector)
        for (const char* mark : {"\xE2\xAD\x90", "\xEF\xB8\x8F"}) {
            size_t size = std::strlen(mark);
            if (t.size() > size && t.compare(t.size() - size, size, mark) == 0) {
                t.erase(t.size() - size);
                changed = true;
            }
        }
        if (changed) continue;
        // The date the provider added it: "23.09.2026"
        if (t.size() > 12 && isYear(t, t.size() - 4) && t[t.size() - 5] == '.' && t[t.size() - 8] == '.' &&
            std::isdigit(static_cast<unsigned char>(t[t.size() - 6])) &&
            std::isdigit(static_cast<unsigned char>(t[t.size() - 9])) &&
            std::isdigit(static_cast<unsigned char>(t[t.size() - 10]))) {
            t.erase(t.size() - 10);
            changed = true;
            continue;
        }
        // "(2023)" or "[2023]" at the end
        if (t.size() > 7 && (t.back() == ')' || t.back() == ']')) {
            size_t open = t.find_last_of("([");
            if (open != std::string::npos && open > 0 && t.size() - open == 6 && isYear(t, open + 1)) {
                t.erase(open);
                changed = true;
                continue;
            }
        }
        // " 2014" or "-2014" at the end (not a title that is a year)
        if (t.size() > 6 && isYear(t, t.size() - 4) && (t[t.size() - 5] == ' ' || t[t.size() - 5] == '-')) {
            t.erase(t.size() - 4);
            changed = true;
            continue;
        }
        for (const auto& tag : tags) {
            if (t.size() > tag.size() + 2 && t[t.size() - tag.size() - 1] == ' ' && upperTail(t, tag.size()) == tag) {
                t.erase(t.size() - tag.size());
                changed = true;
                break;
            }
        }
    }
    return t.size() >= 2 ? t : title;
}

brls::Event<>* getChangedEvent() {
    static brls::Event<> changed;
    return &changed;
}

std::vector<Seed> seeds(size_t max) {
    std::vector<Seed> out;
    std::unordered_set<int64_t> seen;
    auto& catalog = Catalog::instance();
    auto add = [&](const LiveM3u8& entry) {
        if (entry.type == 0 || out.size() >= max) return;
        int type = 0, tmdb = catalog.tmdbOf(entry, type);
        if (tmdb <= 0 || !seen.insert(TmdbStore::key(type, tmdb)).second) return;
        const LiveM3u8* item = catalog.find(type, tmdb);
        if (!item) return;  // no longer in the provider's list
        Seed seed;
        seed.item = *item;
        seed.type = type;
        seed.tmdb = tmdb;
        out.push_back(std::move(seed));
    };
    for (const auto& entry : HistoryManager::get()->recent(100)) add(entry);
    for (const auto& entry : FavoriteManager::get()->getFavorites()) add(entry);
    return out;
}

Page build(const Inputs& in) {
    Page page;
    auto& catalog   = Catalog::instance();
    auto hidden     = hiddenShelves();
    auto show       = [&hidden](const char* id) { return std::find(hidden.begin(), hidden.end(), id) == hidden.end(); };
    auto locked     = lockedCategories();
    Seen seen       = seenTitles();
    int64_t now     = static_cast<int64_t>(std::time(nullptr));
    std::vector<int> franchiseIds;
    std::vector<std::pair<int, size_t>> networkCounts;

    // Continue watching: the history's movies with a position, and the newest episode of each series (the next one
    // once it was finished). It needs no TMDB data.
    Shelf cont;
    cont.id    = "continue";
    cont.title = "tsvitch/discover/shelf/continue"_i18n;
    if (show("continue")) {
        std::unordered_set<std::string> seriesDone;
        for (const auto& h : HistoryManager::get()->recent(100)) {
            if (cont.items.size() >= 15) break;
            int64_t position = 0, duration = 0;
            bool started = PlaybackPositionManager::getProgress(h.url, position, duration) && position > 0;
            if (h.type == 1) {
                if (!started) continue;
                const LiveM3u8* item = catalog.findById(1, h.id);
                if (!item || locked.count(item->groupTitle)) continue;
                cont.items.push_back(*item);
                cont.notes.emplace_back("");
                cont.progress.push_back(h.url);
            } else if (h.type == 2 && h.url.rfind(SERIES_SCHEME, 0) != 0 && !h.seriesId.empty()) {
                if (!seriesDone.insert(h.seriesId).second) continue;
                bool finished = WatchedManager::isWatched(h.url);
                if (!started && !finished) continue;
                const LiveM3u8* series = catalog.findSeries(h.seriesId);
                if (!series || locked.count(series->groupTitle)) continue;
                std::string season;
                for (char c : h.groupTitle)
                    if (c >= '0' && c <= '9') season += c;
                cont.items.push_back(*series);
                cont.notes.push_back(started ? brls::getStr("tsvitch/discover/episode_note",
                                                            season.empty() ? "1" : season, h.chno)
                                             : "tsvitch/discover/next_episode"_i18n);
                cont.progress.push_back(started ? h.url : "");
            }
        }
    }

    TmdbStore::instance().read([&](const TmdbStore::Map& map) {
        auto all   = entries(map, locked);
        auto byRef = indexByRef(all);
        auto entryOf = [&byRef](const TmdbRef& ref) -> const Entry* {
            auto it = byRef.find(TmdbStore::key(ref.type, ref.id));
            return it != byRef.end() ? it->second : nullptr;
        };
        std::unordered_set<const LiveM3u8*> seedItems;
        for (const auto& seed : in.seeds) {
            auto e = entryOf({seed.type, seed.tmdb});
            if (e) seedItems.insert(e->item);
        }
        auto posters = [](const char* id, const std::string& title, const std::vector<const Entry*>& list) {
            Shelf shelf;
            shelf.id    = id;
            shelf.title = title;
            for (const auto* e : list) {
                if (shelf.items.size() >= SHELF_SIZE) break;
                shelf.items.push_back(*e->item);
                shelf.notes.emplace_back("");
                shelf.progress.emplace_back("");
            }
            return shelf;
        };

        // The hero: this week's first title with a backdrop, else a new popular one
        std::vector<const Entry*> trending;
        for (const auto& ref : in.trending)
            if (auto e = entryOf(ref)) trending.push_back(e);
        if (show("hero")) {
            for (const auto* e : trending) {
                if (seen.has(*e) || backdropUrl(*e).empty()) continue;
                page.hero.valid = true;
                page.hero.item  = *e->item;
                page.hero.label = "tsvitch/discover/hero/trending"_i18n;
                page.hero.meta  = heroMeta(*e);
                page.hero.backdrop = backdropUrl(*e);
                break;
            }
            if (!page.hero.valid) {
                const Entry* best = nullptr;
                for (const auto& e : all) {
                    if (backdropUrl(e).empty() || seen.has(e) || now - e.item->added > 45 * 24 * 3600) continue;
                    if (!best || e.popularity() > best->popularity()) best = &e;
                }
                if (best) {
                    page.hero.valid    = true;
                    page.hero.item     = *best->item;
                    page.hero.label    = "tsvitch/discover/hero/new"_i18n;
                    page.hero.meta     = heroMeta(*best);
                    page.hero.backdrop = backdropUrl(*best);
                }
            }
        }

        if (!cont.items.empty()) page.shelves.push_back(cont);

        if (show("trending") && !trending.empty())
            page.shelves.push_back(posters("trending", "tsvitch/discover/shelf/trending"_i18n, trending));

        // For you: what TMDB recommends after the watched titles, the newest seeds weigh more
        if (show("for_you") && !in.seeds.empty()) {
            std::unordered_map<const Entry*, double> score;
            double weight = 1.0;
            for (const auto& seed : in.seeds) {
                int rank = 0;
                for (const auto& ref : seed.recommendations) {
                    const Entry* e = entryOf(ref);
                    rank++;
                    if (!e || seen.has(*e) || seedItems.count(e->item)) continue;
                    score[e] += weight * (1.0 - std::min(rank, 20) / 25.0);
                }
                weight *= 0.8;
            }
            std::vector<const Entry*> picked;
            for (const auto& [e, s] : score) picked.push_back(e);
            if (picked.empty()) {
                // No recommendations yet: the genres of the watched titles, the good ones first
                std::map<uint32_t, int> liked;
                for (const auto& seed : in.seeds)
                    if (auto e = entryOf({seed.type, seed.tmdb}))
                        for (uint32_t bit : genre::split(e->genres())) liked[bit]++;
                for (const auto& e : all) {
                    if (seen.has(e) || seedItems.count(e.item)) continue;
                    double s = 0;
                    for (uint32_t bit : genre::split(e.genres())) {
                        auto it = liked.find(bit);
                        if (it != liked.end()) s += it->second;
                    }
                    if (s > 0) {
                        score[&e] = s * (1.0 + quality(e) / 10.0);
                        picked.push_back(&e);
                    }
                }
            }
            std::stable_sort(picked.begin(), picked.end(),
                             [&score](const Entry* a, const Entry* b) { return score[a] > score[b]; });
            if (!picked.empty()) page.shelves.push_back(posters("for_you", "tsvitch/discover/shelf/for_you"_i18n, picked));
        }

        // Because you watched: the newest seed with enough recommendations
        if (show("because")) {
            for (const auto& seed : in.seeds) {
                std::vector<const Entry*> picked;
                for (const auto& ref : seed.recommendations) {
                    const Entry* e = entryOf(ref);
                    if (e && !seen.has(*e) && !seedItems.count(e->item)) picked.push_back(e);
                }
                if (picked.size() < 5) continue;
                page.shelves.push_back(posters(
                    "because", brls::getStr("tsvitch/discover/shelf/because", cleanTitle(seed.item.title)), picked));
                break;
            }
        }

        // Popular today: TMDB's lists, only what the catalogue has
        for (int type : {1, 2}) {
            const char* id = type == 1 ? "popular_movies" : "popular_series";
            if (!show(id)) continue;
            std::vector<const Entry*> picked;
            for (const auto& ref : type == 1 ? in.popularMovies : in.popularSeries)
                if (auto e = entryOf(ref)) picked.push_back(e);
            if (picked.size() >= 3)
                page.shelves.push_back(posters(id, brls::getStr(std::string("tsvitch/discover/shelf/") + id), picked));
        }

        auto newest = [&all](int type) {
            std::vector<const Entry*> list;
            for (const auto& e : all)
                if (e.type == type && e.item->added > 0) list.push_back(&e);
            size_t count = std::min(SHELF_SIZE, list.size());
            std::partial_sort(list.begin(), list.begin() + count, list.end(),
                              [](const Entry* a, const Entry* b) { return a->item->added > b->item->added; });
            list.resize(count);
            return list;
        };
        if (show("new_movies")) {
            auto list = newest(1);
            if (!list.empty()) page.shelves.push_back(posters("new_movies", "tsvitch/discover/shelf/new_movies"_i18n, list));
        }
        if (show("new_series")) {
            auto list = newest(2);
            if (!list.empty()) page.shelves.push_back(posters("new_series", "tsvitch/discover/shelf/new_series"_i18n, list));
        }

        // Genre tiles
        for (int type : {1, 2}) {
            const char* id = type == 1 ? "movie_genres" : "series_genres";
            if (!show(id)) continue;
            Shelf shelf;
            shelf.id    = id;
            shelf.kind  = Shelf::GENRES;
            shelf.title = brls::getStr(std::string("tsvitch/discover/shelf/") + id);
            for (const auto& info : genre::all()) {
                size_t count = 0;
                for (const auto& e : all)
                    if (e.type == type && (e.genres() & info.bit)) count++;
                if (count < 3) continue;
                Collection c;
                c.id     = std::string("genre:") + std::to_string(type) + ":" + info.key;
                c.title  = genre::name(info.bit);
                c.type   = type;
                c.count  = count;
                c.colorA = info.colorA;
                c.colorB = info.colorB;
                shelf.collections.push_back(std::move(c));
            }
            std::stable_sort(shelf.collections.begin(), shelf.collections.end(),
                             [](const Collection& a, const Collection& b) { return a.count > b.count; });
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        auto covers = [&all](const char* id, const std::vector<Def>& defs) {
            Shelf shelf;
            shelf.id    = id;
            shelf.kind  = Shelf::COVERS;
            shelf.title = brls::getStr(std::string("tsvitch/discover/shelf/") + id);
            for (const auto& def : defs) {
                auto c = runDef(def, all, COVER_ITEMS);
                if (c.count >= def.minimum) shelf.collections.push_back(std::move(c));
            }
            return shelf;
        };
        if (show("themes")) {
            auto shelf = covers("themes", themeDefs());
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        // The genre watched most, without the watched titles
        if (show("favorite_genre") && !in.seeds.empty()) {
            std::map<uint32_t, double> liked;
            double weight = 1.0;
            for (const auto& seed : in.seeds) {
                if (auto e = entryOf({seed.type, seed.tmdb}))
                    for (uint32_t bit : genre::split(e->genres())) liked[bit] += weight;
                weight *= 0.9;
            }
            // Drama is in most titles: another genre says more about a taste when it is nearly as frequent
            uint32_t best  = 0;
            double top     = 0;
            for (const auto& [bit, value] : liked) {
                double v = bit == genre::DRAMA ? value * 0.7 : value;
                if (v > top) {
                    top  = v;
                    best = bit;
                }
            }
            if (best) {
                std::vector<const Entry*> picked;
                for (const auto& e : all)
                    if ((e.genres() & best) && !seen.has(e) && !seedItems.count(e.item)) picked.push_back(&e);
                sortEntries(picked, Sort::VOTE);
                if (picked.size() >= 5)
                    page.shelves.push_back(posters(
                        "favorite_genre", brls::getStr("tsvitch/discover/shelf/favorite_genre", genre::name(best)),
                        picked));
            }
        }

        for (int type : {1, 2}) {
            const char* id = type == 1 ? "top_movies" : "top_series";
            if (!show(id)) continue;
            int minVotes = type == 1 ? 1000 : 300;
            std::vector<const Entry*> picked;
            for (const auto& e : all)
                if (e.type == type && e.votes() >= minVotes) picked.push_back(&e);
            if (picked.empty()) {
                // Without TMDB: the provider's rating
                for (const auto& e : all)
                    if (e.type == type && e.item->rating > 0) picked.push_back(&e);
            }
            std::stable_sort(picked.begin(), picked.end(),
                             [](const Entry* a, const Entry* b) { return a->vote() > b->vote(); });
            if (!picked.empty())
                page.shelves.push_back(
                    posters(id, brls::getStr(std::string("tsvitch/discover/shelf/") + id), picked));
        }

        if (show("studios")) {
            std::vector<Def> defs;
            std::unordered_set<int32_t> known;
            for (const auto& studio : studios()) {
                defs.push_back(studioDef(studio));
                if (studio.type == 2) known.insert(studio.ids.begin(), studio.ids.end());
            }
            // The networks of the catalogue's series (TV channels, local platforms), the biggest first
            std::unordered_map<int32_t, size_t> counts;
            for (const auto& e : all)
                if (e.type == 2 && e.meta)
                    for (auto id : e.meta->companies)
                        if (!known.count(id)) counts[id]++;
            for (const auto& [id, count] : counts)
                if (count >= 8) networkCounts.emplace_back(id, count);
            std::sort(networkCounts.begin(), networkCounts.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            if (networkCounts.size() > 12) networkCounts.resize(12);
            auto shelf = covers("studios", defs);
            // The data's networks get their names after the lock (see below)
            for (const auto& [id, count] : networkCounts) {
                auto c = runDef(networkDef(id, ""), all, COVER_ITEMS);
                if (c.count >= 8) shelf.collections.push_back(std::move(c));
            }
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        if (show("world")) {
            auto shelf = covers("world", worldDefs());
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        if (show("awards")) {
            Shelf shelf;
            shelf.id    = "awards";
            shelf.kind  = Shelf::COVERS;
            shelf.title = "tsvitch/discover/shelf/awards"_i18n;
            for (const auto& award : awards()) {
                auto c = awardCollectionOf(award, all, byRef, COVER_ITEMS);
                if (c.count >= 3) shelf.collections.push_back(std::move(c));
            }
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        if (show("decades")) {
            auto shelf = covers("decades", decadeDefs());
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }

        // Film series: two or more of their movies in the catalogue, the most popular series first
        if (show("franchises")) {
            std::unordered_map<int32_t, std::vector<const Entry*>> groups;
            for (const auto& e : all)
                if (e.type == 1 && e.meta && e.meta->collection > 0) groups[e.meta->collection].push_back(&e);
            std::vector<std::pair<float, int32_t>> ranked;
            for (auto& [id, list] : groups) {
                if (list.size() < 2) continue;
                float popularity = 0;
                for (const auto* e : list) popularity += e->popularity();
                ranked.emplace_back(popularity, id);
            }
            std::sort(ranked.begin(), ranked.end(), std::greater<>());
            if (ranked.size() > 20) ranked.resize(20);
            Shelf shelf;
            shelf.id    = "franchises";
            shelf.kind  = Shelf::COVERS;
            shelf.title = "tsvitch/discover/shelf/franchises"_i18n;
            for (const auto& [popularity, id] : ranked) {
                auto& list = groups[id];
                sortEntries(list, Sort::YEAR_ASC);
                Collection c;
                c.id    = "franchise:" + std::to_string(id);
                c.type  = 1;
                c.count = list.size();
                for (const auto* e : list) c.items.push_back(*e->item);
                paint(c);
                franchiseIds.push_back(id);
                shelf.collections.push_back(std::move(c));
            }
            if (!shelf.collections.empty()) page.shelves.push_back(std::move(shelf));
        }
    });

    // Names kept by the TMDB store (its lock is free again)
    auto& store = TmdbStore::instance();
    for (auto& shelf : page.shelves) {
        for (auto& c : shelf.collections) {
            if (c.id.rfind("franchise:", 0) == 0) {
                std::string name = store.name(TmdbStore::COLLECTION, std::atoi(c.id.c_str() + 10));
                c.title          = name.empty() ? "tsvitch/discover/franchise"_i18n : name;
            } else if (c.id.rfind("network:", 0) == 0) {
                std::string name = store.name(TmdbStore::NETWORK, std::atoi(c.id.c_str() + 8));
                c.title          = brls::getStr("tsvitch/discover/network", name.empty() ? "TV" : name);
            }
        }
    }
    return page;
}

std::vector<Collection> genreTiles(int type) {
    std::vector<Collection> out;
    auto locked = lockedCategories();
    TmdbStore::instance().read([&](const TmdbStore::Map& map) {
        auto all = entries(map, locked);
        for (const auto& info : genre::all()) {
            size_t count = 0;
            for (const auto& e : all)
                if (e.type == type && (e.genres() & info.bit)) count++;
            if (count == 0) continue;
            Collection c;
            c.id     = std::string("genre:") + std::to_string(type) + ":" + info.key;
            c.title  = genre::name(info.bit);
            c.type   = type;
            c.count  = count;
            c.colorA = info.colorA;
            c.colorB = info.colorB;
            out.push_back(std::move(c));
        }
    });
    std::stable_sort(out.begin(), out.end(), [](const Collection& a, const Collection& b) { return a.count > b.count; });
    return out;
}

Collection collection(const std::string& id) {
    Collection result;
    result.id   = id;
    auto locked = lockedCategories();
    auto colon  = id.find(':');
    std::string kind = id.substr(0, colon), rest = colon == std::string::npos ? "" : id.substr(colon + 1);
    TmdbStore::instance().read([&](const TmdbStore::Map& map) {
        auto all = entries(map, locked);
        if (kind == "genre") {
            // genre:<type>:<key>
            auto second            = rest.find(':');
            int type               = std::atoi(rest.substr(0, second).c_str());
            const genre::Info* info = second == std::string::npos ? nullptr : genre::findByKey(rest.substr(second + 1));
            if (info) result = genreCollectionOf(type, info->bit, all, 0);
            return;
        }
        if (kind == "award") {
            auto byRef = indexByRef(all);
            for (const auto& award : awards())
                if (award.id == rest) result = awardCollectionOf(award, all, byRef, 0);
            return;
        }
        if (kind == "franchise") {
            int32_t franchise = std::atoi(rest.c_str());
            Def def;
            def.id    = id;
            def.type  = 1;
            def.sort  = Sort::YEAR_ASC;
            def.match = [franchise](const Entry& e) { return e.meta && e.meta->collection == franchise; };
            result    = runDef(def, all, 0);
            return;
        }
        if (kind == "network") {
            result = runDef(networkDef(std::atoi(rest.c_str()), ""), all, 0);
            return;
        }
        if (kind == "studio") {
            for (const auto& studio : studios())
                if (rest == studio.id) result = runDef(studioDef(studio), all, 0);
            return;
        }
        for (const auto* defs : {&themeDefs(), &worldDefs(), &decadeDefs()})
            for (const auto& def : *defs)
                if (def.id == id) result = runDef(def, all, 0);
    });
    auto& store = TmdbStore::instance();
    if (kind == "franchise") {
        std::string name = store.name(TmdbStore::COLLECTION, std::atoi(rest.c_str()));
        result.title     = name.empty() ? "tsvitch/discover/franchise"_i18n : name;
    } else if (kind == "network") {
        std::string name = store.name(TmdbStore::NETWORK, std::atoi(rest.c_str()));
        result.title     = brls::getStr("tsvitch/discover/network", name.empty() ? "TV" : name);
    }
    return result;
}

}  // namespace tsvitch::discover
