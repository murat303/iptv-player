#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <borealis/core/event.hpp>

namespace tsvitch {

/// A title in a TMDB list: 1 = movie, 2 = series
struct TmdbRef {
    int type = 1;
    int id   = 0;
};

/// Someone of the cast or the director, for the detail screen
struct TmdbPerson {
    std::string name;
    std::string role;   // the character, or "director" for the director
    std::string photo;  // picture url (small size), empty when TMDB has none
};

/// What the detail screen of a title shows from TMDB
struct TmdbDetails {
    bool ok       = false;
    float vote    = 0;
    int votes     = 0;
    int collection = 0;  // the film series of a movie
    std::string overview;
    std::vector<TmdbPerson> people;  // the director first (movies), then the cast
    std::vector<TmdbRef> recommendations;
};

/**
 * The app's connection to TMDB (themoviedb.org), for the discovery screen and the genre pages.
 *
 * The provider's lists already carry the TMDB id of most movies and series, so TMDB is never searched by name:
 * each title of the catalogue is asked about once (genres, year, votes, keywords...) and kept on the SD card for
 * six months at most, and a few lists (trending this week, recommendations) are kept for a day or a month.
 * Five worker threads send the requests with a gap (about 30 a second in all), wait while a video plays and stop at
 * once when the app closes. What came is saved every minute, so a new start goes on where the last one stopped. Nothing goes to TMDB without a key (built in, or the user's own in <config>/tmdb_key.txt)
 * or when the setting is off.
 */
class TmdbService {
public:
    using ListCallback    = std::function<void(const std::vector<TmdbRef>&)>;
    using DetailsCallback = std::function<void(const TmdbDetails&)>;

    static TmdbService& instance();

    /// A key exists and the setting allows TMDB
    bool enabled() const;
    bool hasKey() const;
    /// TMDB refused the key (401): nothing is asked until the app starts again
    bool keyRejected() const { return rejected; }

    /// Starts the workers (once) and asks for what the catalogue's titles miss. UI thread; call it again when the
    /// catalogue changed.
    void refresh();

    /// The app closes: the workers stop at once (a request on its way is cut)
    void stop();

    /// A video plays: fetching the catalogue waits (lists the screens ask for still go)
    void setPaused(bool paused);

    /// The catalogue's titles with TMDB data, and all of them (the progress of the first fetch)
    void progress(size_t& done, size_t& total) const;

    /// A TMDB list such as "trending/movie/week" or "movie/603/recommendations" (pages of 20). The saved copy comes
    /// at once, even an old one; a list older than maxAge is fetched again and the changed event fires then. No
    /// saved copy: done is called when the list arrived (empty on failure). done runs on the UI thread.
    void list(const std::string& path, int pages, int64_t maxAge, const ListCallback& done);

    /// The rating, cast and recommendations of one title (one request, kept in memory while the app runs); done runs
    /// on the UI thread, with ok false when TMDB is off or did not answer
    void details(int type, int id, const DetailsCallback& done);

    /// Forgets the TMDB data and lists (Settings)
    void clearData();

    /// TMDB was turned off: nothing more is asked (refresh starts again)
    void stopFetching();

    /// New data arrived (at most every 2 s while fetching); UI thread
    brls::Event<>* getChangedEvent() { return &changed; }

private:
    struct ListJob {
        std::string path;
        int pages = 1;
        std::vector<ListCallback> waiting;
    };
    struct CachedList {
        int64_t time = 0;
        std::vector<TmdbRef> refs;
    };
    struct DetailJob {
        int type = 1;
        int id   = 0;
        std::vector<DetailsCallback> waiting;
    };

    TmdbService() = default;
    void startWorkers();
    void worker(int index);
    void computePending();
    void notifyChanged(bool now);
    void loadLists();
    void saveLists(bool force);
    std::string apiKey() const;
    std::string language() const;

    mutable std::mutex mutex;
    std::condition_variable wake, exited;
    std::deque<ListJob> listJobs;
    std::deque<DetailJob> detailJobs;
    std::map<int64_t, TmdbDetails> detailCache;
    std::deque<int64_t> detailOrder;  // the oldest cached details go first
    std::deque<TmdbRef> pending;
    std::map<std::string, CachedList> lists;
    std::vector<std::thread> workers;
    int running         = 0;
    size_t total        = 0;
    size_t done         = 0;
    int failures        = 0;
    int64_t resumeAt    = 0;  // steady ms: fetching the catalogue waits until then (after errors, 429)
    bool listsDirty     = false;
    int64_t listsSaved  = 0;
    bool listsLoaded    = false;
    bool storeReady     = false;
    bool refreshWaiting = false;
    std::atomic<bool> stopping{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> rejected{false};
    std::atomic<bool> notifyQueued{false};
    std::atomic<int64_t> lastNotify{0};
    brls::Event<> changed;
};

}  // namespace tsvitch
