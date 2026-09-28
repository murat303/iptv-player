#include "api/tmdb.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

#include "api/tsvitch/util/http.hpp"
#include "core/Catalog.hpp"
#include "core/FavoriteManager.hpp"
#include "core/HistoryManager.hpp"
#include "core/TmdbStore.hpp"
#include "utils/config_helper.hpp"
#include "utils/genres.hpp"

#ifndef TMDB_API_KEY
#define TMDB_API_KEY ""
#endif

namespace tsvitch {

namespace {

constexpr int WORKERS          = 5;
constexpr int GAP_MS           = 150;  // from one request of a worker to its next: at most ~33 a second for all
                                       // (TMDB allows about 40 a second from one address)
constexpr size_t MAX_LISTS     = 200;  // saved TMDB lists (recommendations of old seeds are dropped first)
constexpr int64_t PAUSE_ERRORS = 120;  // seconds without catalogue requests after several errors in a row

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t nowSeconds() { return static_cast<int64_t>(std::time(nullptr)); }

std::string trim(std::string text) {
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string baseUrl() {
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
    // The desktop test harness points the app at its own fake TMDB
    if (const char* base = std::getenv("IPTV_TMDB_BASE")) return base;
#endif
    return "https://api.themoviedb.org/3";
}

std::filesystem::path dataDir() { return std::filesystem::path(ProgramConfig::instance().getConfigDir()) / "tmdb"; }

/// The gap between two requests of a worker; the desktop test harness shortens it for its fake TMDB
int gapMs() {
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
    if (const char* gap = std::getenv("IPTV_TMDB_GAP")) return std::max(0, std::atoi(gap));
#endif
    return GAP_MS;
}

double number(const nlohmann::json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : 0;
}

std::string text(const nlohmann::json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : "";
}

void addIds(const nlohmann::json& object, const char* key, std::vector<int32_t>& ids, size_t max) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_array()) return;
    for (const auto& entry : *it) {
        if (ids.size() >= max) break;
        if (!entry.is_object()) continue;
        auto id = static_cast<int32_t>(number(entry, "id"));
        if (id > 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
}

void copyCode(char* out, const std::string& code) {
    out[0] = code.size() > 0 ? static_cast<char>(std::tolower(static_cast<unsigned char>(code[0]))) : 0;
    out[1] = code.size() > 1 ? static_cast<char>(std::tolower(static_cast<unsigned char>(code[1]))) : 0;
    out[2] = 0;
}

/// The fields of /movie/{id} or /tv/{id} (with append_to_response=keywords) the app keeps; names gets the film
/// series' and the networks' names
TmdbMeta parseMeta(int type, const nlohmann::json& j, TmdbStore::Names& names) {
    TmdbMeta meta;
    meta.fetched = nowSeconds();
    if (auto genres = j.find("genres"); genres != j.end() && genres->is_array())
        for (const auto& g : *genres)
            if (g.is_object()) meta.genres |= genre::fromTmdb(static_cast<int>(number(g, "id")));
    std::string date = text(j, type == 1 ? "release_date" : "first_air_date");
    if (date.size() >= 4) meta.year = static_cast<int16_t>(std::atoi(date.substr(0, 4).c_str()));
    meta.popularity = static_cast<float>(number(j, "popularity"));
    meta.vote       = static_cast<float>(number(j, "vote_average"));
    meta.votes      = static_cast<int32_t>(number(j, "vote_count"));
    copyCode(meta.lang, text(j, "original_language"));
    meta.backdrop = text(j, "backdrop_path");
    if (type == 1) {
        meta.revenue = static_cast<float>(number(j, "revenue") / 1e6);
        if (auto c = j.find("belongs_to_collection"); c != j.end() && c->is_object()) {
            meta.collection = static_cast<int32_t>(number(*c, "id"));
            names.emplace_back(TmdbStore::nameKey(TmdbStore::COLLECTION, meta.collection), text(*c, "name"));
        }
        if (auto countries = j.find("production_countries"); countries != j.end() && countries->is_array() &&
                                                              !countries->empty() && (*countries)[0].is_object())
            copyCode(meta.country, text((*countries)[0], "iso_3166_1"));
        addIds(j, "production_companies", meta.companies, 12);
        if (auto k = j.find("keywords"); k != j.end() && k->is_object()) addIds(*k, "keywords", meta.keywords, 40);
    } else {
        if (auto countries = j.find("origin_country"); countries != j.end() && countries->is_array() &&
                                                        !countries->empty() && (*countries)[0].is_string())
            copyCode(meta.country, (*countries)[0].get<std::string>());
        std::string status = text(j, "status");
        meta.status  = status == "Ended" || status == "Canceled" ? 1 : status.empty() ? 0 : 2;
        meta.seasons = static_cast<uint16_t>(number(j, "number_of_seasons"));
        addIds(j, "networks", meta.companies, 6);
        if (auto networks = j.find("networks"); networks != j.end() && networks->is_array())
            for (const auto& network : *networks)
                if (network.is_object())
                    names.emplace_back(
                        TmdbStore::nameKey(TmdbStore::NETWORK, static_cast<int>(number(network, "id"))),
                        text(network, "name"));
        if (auto k = j.find("keywords"); k != j.end() && k->is_object()) addIds(*k, "results", meta.keywords, 40);
    }
    return meta;
}

/// The detail screen's part of /movie/{id} or /tv/{id} with append_to_response=credits,recommendations
TmdbDetails parseDetails(int type, const nlohmann::json& j) {
    TmdbDetails d;
    d.ok       = true;
    d.vote     = static_cast<float>(number(j, "vote_average"));
    d.votes    = static_cast<int>(number(j, "vote_count"));
    d.overview = text(j, "overview");
    if (auto c = j.find("belongs_to_collection"); c != j.end() && c->is_object())
        d.collection = static_cast<int>(number(*c, "id"));
    auto add = [](std::string& list, const std::string& name) {
        if (name.empty()) return;
        if (!list.empty()) list += ", ";
        list += name;
    };
    if (auto credits = j.find("credits"); credits != j.end() && credits->is_object()) {
        if (auto crew = credits->find("crew"); crew != credits->end() && crew->is_array())
            for (const auto& person : *crew)
                if (person.is_object() && text(person, "job") == "Director") add(d.directors, text(person, "name"));
        if (auto cast = credits->find("cast"); cast != credits->end() && cast->is_array()) {
            int count = 0;
            for (const auto& person : *cast) {
                if (count == 6) break;
                if (!person.is_object()) continue;
                add(d.cast, text(person, "name"));
                count++;
            }
        }
    }
    if (auto rec = j.find("recommendations"); rec != j.end() && rec->is_object())
        if (auto results = rec->find("results"); results != rec->end() && results->is_array())
            for (const auto& result : *results) {
                if (!result.is_object()) continue;
                std::string media = text(result, "media_type");
                int refType       = media == "tv" ? 2 : media == "movie" ? 1 : type;
                int id            = static_cast<int>(number(result, "id"));
                if (id > 0) d.recommendations.push_back({refType, id});
            }
    return d;
}

}  // namespace

TmdbService& TmdbService::instance() {
    // Never destroyed: a worker that could not stop in time must still find it while the app closes
    static auto* service = new TmdbService();
    return *service;
}

std::string TmdbService::apiKey() const {
    static const std::string key = []() {
        std::string found;
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
        if (const char* env = std::getenv("IPTV_TMDB_KEY")) found = env;
#endif
        if (found.empty()) {
            // The user's own key wins over the built-in one
            std::ifstream in(std::filesystem::path(ProgramConfig::instance().getConfigDir()) / "tmdb_key.txt");
            std::getline(in, found);
        }
        found = trim(found);
        if (found.empty()) found = TMDB_API_KEY;
        return found;
    }();
    return key;
}

bool TmdbService::hasKey() const { return !apiKey().empty(); }

bool TmdbService::enabled() const {
    return hasKey() && !rejected && ProgramConfig::instance().getSettingItem(SettingItem::TMDB_ENABLED, 1) != 0;
}

std::string TmdbService::language() const {
    std::string locale = brls::Application::getLocale();
    if (locale.rfind("tr", 0) == 0) return "tr-TR";
    if (locale.rfind("it", 0) == 0) return "it-IT";
    if (locale.rfind("pt", 0) == 0) return "pt-BR";
    return "en-US";
}

void TmdbService::startWorkers() {
    if (!workers.empty() || stopping) return;
    this->loadLists();
    // New or changed lists: fetch what their titles miss
    Catalog::instance().getChangedEvent()->subscribe([]() { TmdbService::instance().refresh(); });
    running = WORKERS;
    for (int i = 0; i < WORKERS; i++) workers.emplace_back([this, i]() { this->worker(i); });
    brls::Logger::info("TMDB: {} workers started", WORKERS);
}

void TmdbService::refresh() {
    // Only an Xtream account's lists carry TMDB ids
    if (!enabled() || ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) != 1) return;
    this->startWorkers();
    bool ready;
    {
        std::lock_guard<std::mutex> lock(mutex);
        ready = storeReady;
    }
    if (ready)
        this->computePending();
    else
        refreshWaiting = true;
}

void TmdbService::computePending() {
    if (!enabled()) return;
    auto& catalog = Catalog::instance();
    if (!catalog.isLoaded()) {
        // The changed event of the catalogue calls refresh() again
        catalog.ensureLoaded();
        return;
    }
    int64_t now = nowSeconds();
    std::vector<TmdbRef> order;
    std::unordered_set<int64_t> seen;
    auto add = [&order, &seen](int type, int id) {
        if (id <= 0 || !seen.insert(TmdbStore::key(type, id)).second) return;
        order.push_back({type, id});
    };
    // First what was watched or marked (the recommendations start from them), then the newest titles, then the
    // rest by rating (the genre pages need every movie)
    for (const auto& item : HistoryManager::get()->recent(100)) {
        int type = 0, id = catalog.tmdbOf(item, type);
        add(type, id);
    }
    for (const auto& item : FavoriteManager::get()->getFavorites()) {
        int type = 0, id = catalog.tmdbOf(item, type);
        add(type, id);
    }
    auto newest = [](const std::vector<LiveM3u8>& items, size_t count) {
        std::vector<const LiveM3u8*> sorted;
        sorted.reserve(items.size());
        for (const auto& item : items) sorted.push_back(&item);
        count = std::min(count, sorted.size());
        std::partial_sort(sorted.begin(), sorted.begin() + count, sorted.end(),
                          [](const LiveM3u8* a, const LiveM3u8* b) { return a->added > b->added; });
        sorted.resize(count);
        return sorted;
    };
    for (const auto* item : newest(catalog.items(1), 300)) add(1, item->tmdb);
    for (const auto* item : newest(catalog.items(2), 100)) add(2, item->tmdb);
    for (int type : {1, 2}) {
        std::vector<const LiveM3u8*> byRating;
        for (const auto& item : catalog.items(type)) byRating.push_back(&item);
        std::stable_sort(byRating.begin(), byRating.end(),
                         [](const LiveM3u8* a, const LiveM3u8* b) { return a->rating > b->rating; });
        for (const auto* item : byRating) add(type, item->tmdb);
    }

    std::deque<TmdbRef> need;
    size_t have = 0;
    auto& store = TmdbStore::instance();
    for (const auto& ref : order) {
        if (store.needsFetch(ref.type, ref.id, now))
            need.push_back(ref);
        else
            have++;
    }
    brls::Logger::info("TMDB: {} titles in the catalogue, {} to fetch", order.size(), need.size());
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.swap(need);
        total = order.size();
        done  = have;
    }
    wake.notify_all();
}

void TmdbService::progress(size_t& doneOut, size_t& totalOut) const {
    std::lock_guard<std::mutex> lock(mutex);
    doneOut  = done;
    totalOut = total;
}

void TmdbService::setPaused(bool value) {
    paused = value;
    if (!value) wake.notify_all();
}

void TmdbService::notifyChanged(bool now) {
    int64_t time = steadyMs();
    if (!now && time - lastNotify.load() < 2000) return;
    if (notifyQueued.exchange(true)) return;
    lastNotify = time;
    brls::sync([]() {
        auto& service        = TmdbService::instance();
        service.notifyQueued = false;
        service.changed.fire();
    });
}

void TmdbService::list(const std::string& path, int pages, int64_t maxAge, const ListCallback& done) {
    if (!enabled()) {
        if (done) done({});
        return;
    }
    this->startWorkers();
    std::vector<TmdbRef> cached;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = lists.find(path);
        bool fresh = false;
        if (it != lists.end()) {
            cached = it->second.refs;
            have   = true;
            fresh  = nowSeconds() - it->second.time < maxAge;
        }
        if (!fresh) {
            bool queued = false;
            for (auto& job : listJobs) {
                if (job.path != path) continue;
                if (!have && done) job.waiting.push_back(done);
                queued = true;
            }
            if (!queued) {
                ListJob job;
                job.path  = path;
                job.pages = std::max(1, pages);
                if (!have && done) job.waiting.push_back(done);
                listJobs.push_back(std::move(job));
                wake.notify_one();
            }
        }
    }
    if (have && done) done(cached);
}

void TmdbService::stopFetching() {
    std::vector<ListCallback> waiting;
    std::vector<DetailsCallback> waitingDetails;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.clear();
        for (auto& job : listJobs)
            for (auto& callback : job.waiting) waiting.push_back(std::move(callback));
        listJobs.clear();
        for (auto& job : detailJobs)
            for (auto& callback : job.waiting) waitingDetails.push_back(std::move(callback));
        detailJobs.clear();
    }
    // Screens waiting for a list or details get an empty answer
    for (const auto& callback : waiting)
        if (callback) callback({});
    for (const auto& callback : waitingDetails)
        if (callback) callback({});
}

void TmdbService::details(int type, int id, const DetailsCallback& done) {
    if (!enabled() || id <= 0) {
        if (done) done({});
        return;
    }
    this->startWorkers();
    int64_t key = TmdbStore::key(type, id);
    TmdbDetails cached;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = detailCache.find(key);
        if (it != detailCache.end()) {
            cached = it->second;
            have   = true;
        } else {
            bool queued = false;
            for (auto& job : detailJobs)
                if (job.type == type && job.id == id) {
                    job.waiting.push_back(done);
                    queued = true;
                }
            if (!queued) {
                DetailJob job;
                job.type = type;
                job.id   = id;
                job.waiting.push_back(done);
                detailJobs.push_back(std::move(job));
                wake.notify_one();
            }
        }
    }
    if (have && done) done(cached);
}

void TmdbService::clearData() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.clear();
        lists.clear();
        listsDirty = true;
        done       = 0;
    }
    TmdbStore::instance().clear();
    this->saveLists(true);
    if (enabled()) this->refresh();
}

void TmdbService::loadLists() {
    if (listsLoaded) return;
    listsLoaded = true;
    std::ifstream in(dataDir() / "lists.json");
    if (!in) return;
    auto json = nlohmann::json::parse(in, nullptr, false);
    if (!json.is_object() || !json.contains("lists") || !json["lists"].is_object()) return;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = json["lists"].begin(); it != json["lists"].end(); ++it) {
        if (!it->is_object()) continue;
        CachedList list;
        list.time = static_cast<int64_t>(number(*it, "t"));
        if (auto refs = it->find("r"); refs != it->end() && refs->is_array()) {
            for (const auto& ref : *refs)
                if (ref.is_array() && ref.size() == 2 && ref[0].is_number() && ref[1].is_number())
                    list.refs.push_back({ref[0].get<int>(), ref[1].get<int>()});
        }
        lists[it.key()] = std::move(list);
    }
}

void TmdbService::saveLists(bool force) {
    nlohmann::json json;
    {
        std::lock_guard<std::mutex> lock(mutex);
        int64_t now = nowSeconds();
        if (!listsDirty || (!force && now - listsSaved < 30)) return;
        listsDirty = false;
        listsSaved = now;
        // The oldest lists go first when there are too many
        std::vector<std::pair<int64_t, std::string>> ages;
        for (const auto& [path, list] : lists) ages.emplace_back(list.time, path);
        std::sort(ages.begin(), ages.end(), std::greater<>());
        if (ages.size() > MAX_LISTS) {
            for (size_t i = MAX_LISTS; i < ages.size(); i++) lists.erase(ages[i].second);
        }
        json["v"]     = 1;
        auto& entries = json["lists"];
        entries       = nlohmann::json::object();
        for (const auto& [path, list] : lists) {
            nlohmann::json refs = nlohmann::json::array();
            for (const auto& ref : list.refs) refs.push_back({ref.type, ref.id});
            entries[path] = {{"t", list.time}, {"r", refs}};
        }
    }
    static std::mutex fileMutex;
    std::lock_guard<std::mutex> fileLock(fileMutex);
    std::error_code ec;
    std::filesystem::create_directories(dataDir(), ec);
    auto file = dataDir() / "lists.json", temp = dataDir() / "lists.json.tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) return;
        out << json.dump();
        if (!out) return;
    }
    std::filesystem::remove(file, ec);
    std::filesystem::rename(temp, file, ec);
}

void TmdbService::worker(int index) {
    if (index == 0) {
        // The saved data first: only titles without it are asked for
        TmdbStore::instance().load();
        {
            std::lock_guard<std::mutex> lock(mutex);
            storeReady = true;
        }
        brls::sync([]() {
            auto& service = TmdbService::instance();
            if (service.refreshWaiting) {
                service.refreshWaiting = false;
                service.computePending();
            }
        });
        wake.notify_all();
    }

    const std::string key  = apiKey();
    const bool bearer      = key.size() > 40;  // a v4 read token rather than a v3 key
    const std::string base = baseUrl();
    cpr::Session session;
    session.SetTimeout(cpr::Timeout{15000});
    session.SetConnectTimeout(cpr::ConnectTimeout{8000});
    session.SetVerifySsl(HTTP::VERIFY);
    session.SetProxies(cpr::Proxies{HTTP::PROXIES});
    cpr::Header header{{"User-Agent", "IPTVPlayer/1.0"}, {"Accept", "application/json"}};
    if (bearer) header["Authorization"] = "Bearer " + key;
    session.SetHeader(header);
    session.SetProgressCallback(cpr::ProgressCallback(
        [this](cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t) -> bool {
            return !stopping.load();
        }));
    // The key never appears in a log: it is a parameter or a header, and urls are not logged
    auto get = [&](const std::string& path, cpr::Parameters params) {
        if (!bearer) params.Add({"api_key", key});
        session.SetUrl(cpr::Url{base + path});
        session.SetParameters(params);
        return session.Get();
    };
    int64_t lastRequest = 0;
    auto waitGap = [this, &lastRequest](int gapMs) {
        while (!stopping && steadyMs() < lastRequest + gapMs) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lastRequest = steadyMs();
        return !stopping.load();
    };

    while (!stopping) {
        ListJob job;
        DetailJob detailJob;
        TmdbRef ref;
        bool haveJob = false, haveRef = false, haveDetail = false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            auto canFetch = [this]() {
                return !paused && storeReady && !pending.empty() && steadyMs() >= resumeAt;
            };
            wake.wait_for(lock, std::chrono::milliseconds(500), [this, &canFetch]() {
                return stopping || !detailJobs.empty() || !listJobs.empty() || canFetch();
            });
            if (stopping) break;
            if (!detailJobs.empty()) {
                detailJob = std::move(detailJobs.front());
                detailJobs.pop_front();
                haveDetail = true;
            } else if (!listJobs.empty()) {
                job = std::move(listJobs.front());
                listJobs.pop_front();
                haveJob = true;
            } else if (canFetch()) {
                ref = pending.front();
                pending.pop_front();
                haveRef = true;
            }
        }
        if (!haveJob && !haveRef && !haveDetail) {
            TmdbStore::instance().save(false);
            this->saveLists(false);
            continue;
        }

        if (haveDetail) {
            // A detail screen waits for it: one request with the cast and the recommendations
            TmdbDetails result;
            std::string path = std::string(detailJob.type == 1 ? "/movie/" : "/tv/") + std::to_string(detailJob.id);
            std::string lang = language();
            if (waitGap(gapMs())) {
                auto r = get(path, cpr::Parameters{{"language", lang}, {"append_to_response", "credits,recommendations"}});
                if (r.status_code == 401) rejected = true;
                if (r.status_code == 200) {
                    auto json = nlohmann::json::parse(r.text, nullptr, false);
                    if (json.is_object()) result = parseDetails(detailJob.type, json);
                }
            }
            // TMDB leaves the overview empty when it has none in the app's language: the English one then
            if (result.ok && result.overview.empty() && lang != "en-US" && !stopping && waitGap(gapMs())) {
                auto r = get(path, cpr::Parameters{{"language", "en-US"}});
                if (r.status_code == 200) {
                    auto json = nlohmann::json::parse(r.text, nullptr, false);
                    if (json.is_object()) result.overview = text(json, "overview");
                }
            }
            if (stopping) break;
            if (result.ok) {
                std::lock_guard<std::mutex> lock(mutex);
                int64_t key = TmdbStore::key(detailJob.type, detailJob.id);
                if (!detailCache.count(key)) detailOrder.push_back(key);
                detailCache[key] = result;
                while (detailOrder.size() > 40) {
                    detailCache.erase(detailOrder.front());
                    detailOrder.pop_front();
                }
            }
            brls::sync([waiting = detailJob.waiting, result]() {
                for (const auto& callback : waiting)
                    if (callback) callback(result);
            });
            continue;
        }

        if (haveJob) {
            // A list the screen waits for: its pages one after the other
            int type = job.path.rfind("tv", 0) == 0 || job.path.find("/tv/") != std::string::npos ? 2 : 1;
            std::vector<TmdbRef> refs;
            std::set<int64_t> seenRefs;
            bool ok = true;
            for (int page = 1; page <= job.pages && !stopping; page++) {
                if (!waitGap(gapMs())) break;
                auto r = get("/" + job.path, cpr::Parameters{{"language", language()}, {"page", std::to_string(page)}});
                if (r.status_code == 401) rejected = true;
                if (r.status_code != 200) {
                    brls::Logger::warning("TMDB list {}: status {} {}", job.path, r.status_code, r.error.message);
                    ok = page > 1;
                    break;
                }
                auto json = nlohmann::json::parse(r.text, nullptr, false);
                if (!json.is_object() || !json.contains("results") || !json["results"].is_array()) {
                    ok = page > 1;
                    break;
                }
                for (const auto& result : json["results"]) {
                    if (!result.is_object()) continue;
                    std::string media = text(result, "media_type");
                    if (media == "person") continue;
                    int refType = media == "tv" ? 2 : media == "movie" ? 1 : type;
                    int id      = static_cast<int>(number(result, "id"));
                    if (id > 0 && seenRefs.insert(TmdbStore::key(refType, id)).second) refs.push_back({refType, id});
                }
                if (page >= static_cast<int>(number(json, "total_pages"))) break;
            }
            if (stopping) break;
            if (ok) {
                std::lock_guard<std::mutex> lock(mutex);
                lists[job.path] = {nowSeconds(), refs};
                listsDirty      = true;
            } else {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = lists.find(job.path);
                if (it != lists.end()) refs = it->second.refs;
            }
            if (!job.waiting.empty()) {
                brls::sync([waiting = job.waiting, refs]() {
                    for (const auto& callback : waiting) callback(refs);
                });
            }
            if (ok) this->notifyChanged(true);
            continue;
        }

        // One title of the catalogue
        if (!waitGap(gapMs())) break;
        auto r = get(std::string(ref.type == 1 ? "/movie/" : "/tv/") + std::to_string(ref.id),
                     cpr::Parameters{{"language", language()}, {"append_to_response", "keywords"}});
        if (stopping) break;
        bool stored = false;
        if (r.status_code == 200) {
            auto json = nlohmann::json::parse(r.text, nullptr, false);
            if (json.is_object()) {
                TmdbStore::Names names;
                auto meta = parseMeta(ref.type, json, names);
                TmdbStore::instance().put(ref.type, ref.id, std::move(meta), names);
                stored = true;
            }
        } else if (r.status_code == 404) {
            TmdbMeta meta;
            meta.fetched = nowSeconds();
            meta.missing = true;
            TmdbStore::instance().put(ref.type, ref.id, meta);
            stored = true;
        } else if (r.status_code == 401) {
            brls::Logger::error("TMDB: the key was refused, nothing more is asked");
            rejected = true;
            std::lock_guard<std::mutex> lock(mutex);
            pending.clear();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stored) {
                done++;
                failures = 0;
                if (done % 1000 == 0) brls::Logger::info("TMDB: {} of {} titles", done, total);
            } else if (!rejected) {
                // 429 (too many requests), 5xx, network: the title is asked again later
                pending.push_back(ref);
                if (r.status_code == 429) {
                    int seconds = 2;
                    auto after  = r.header.find("Retry-After");
                    if (after != r.header.end()) seconds = std::max(1, std::min(60, std::atoi(after->second.c_str())));
                    resumeAt = steadyMs() + seconds * 1000;
                } else if (++failures >= 5) {
                    brls::Logger::warning("TMDB: {} errors in a row (status {} {}), waiting", failures, r.status_code,
                                          r.error.message);
                    failures = 0;
                    resumeAt = steadyMs() + PAUSE_ERRORS * 1000;
                }
            }
        }
        bool finished;
        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = pending.empty();
        }
        this->notifyChanged(finished || rejected);
        // What came so far is on the SD card at most a minute later: a crash or a power cut loses little
        TmdbStore::instance().save(finished);
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        running--;
    }
    exited.notify_all();
}

void TmdbService::stop() {
    if (workers.empty()) return;
    stopping = true;
    wake.notify_all();
    bool all;
    {
        std::unique_lock<std::mutex> lock(mutex);
        all = exited.wait_for(lock, std::chrono::seconds(2), [this]() { return running == 0; });
    }
    // A request that cannot be cut (a name lookup) must not hold the app: such a worker is left behind
    for (auto& thread : workers) {
        if (all)
            thread.join();
        else
            thread.detach();
    }
    workers.clear();
    TmdbStore::instance().save(true);
    this->saveLists(true);
    brls::Logger::info("TMDB: stopped");
}

}  // namespace tsvitch
