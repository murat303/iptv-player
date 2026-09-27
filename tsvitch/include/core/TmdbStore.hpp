#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// What TMDB says about one movie or series, kept for the discovery screen and the genre pages
struct TmdbMeta {
    int64_t fetched    = 0;      // Unix time it was read from TMDB
    bool missing       = false;  // TMDB does not know the id (asked again after a month)
    uint32_t genres    = 0;      // utils/genres.hpp bits
    int16_t year       = 0;
    float popularity   = 0;
    float vote         = 0;      // average vote 0-10
    int32_t votes      = 0;
    float revenue      = 0;      // box office in millions of dollars (movies)
    int32_t collection = 0;      // the film series a movie belongs to (TMDB collection id)
    char lang[3]       = {0, 0, 0};  // original language ("tr", "ko")
    char country[3]    = {0, 0, 0};  // series: first origin country; movies: first production country
    uint8_t status     = 0;          // series: 1 ended, 2 still running
    uint16_t seasons   = 0;          // series
    std::vector<int32_t> keywords;
    std::vector<int32_t> companies;  // movies: production companies; series: networks (channels, platforms)
    std::string backdrop;            // TMDB image path ("/abc.jpg")
};

/// The TMDB data of the catalogue's movies and series on the SD card (<config dir>/tmdb/meta.bin). Written by the
/// TMDB workers, read by the screens; every access takes the lock.
class TmdbStore {
public:
    using Map = std::unordered_map<int64_t, TmdbMeta>;

    static TmdbStore& instance();

    static int64_t key(int type, int id) { return (static_cast<int64_t>(type) << 32) | static_cast<uint32_t>(id); }

    /// Reads the file once (a worker thread calls it before its first request)
    void load();
    bool isLoaded() const;

    /// fn(map) under the lock: many lookups at once without copying
    template <typename F>
    void read(F&& fn) const {
        std::lock_guard<std::mutex> lock(mutex);
        fn(map);
    }

    bool get(int type, int id, TmdbMeta& meta) const;

    /// Names met in the data: a movie's film series, a series' network
    enum NameKind { COLLECTION = 1, NETWORK = 2 };
    using Names = std::vector<std::pair<int64_t, std::string>>;
    static int64_t nameKey(NameKind kind, int id) { return key(kind, id); }

    void put(int type, int id, TmdbMeta meta, const Names& names = {});

    /// Not asked yet, or asked long ago (TMDB allows keeping its data for six months)
    bool needsFetch(int type, int id, int64_t now) const;

    std::string name(NameKind kind, int id) const;

    size_t size() const;

    /// Writes the file when something changed and the last write is older than a minute (force: now)
    void save(bool force);

    /// Settings: forgets everything (the data is fetched again)
    void clear();

private:
    mutable std::mutex mutex;
    Map map;
    std::unordered_map<int64_t, std::string> names;
    bool loaded      = false;
    bool dirty       = false;
    int64_t lastSave = 0;
};
