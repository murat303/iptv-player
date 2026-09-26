

#pragma once

#include "view/auto_tab_frame.hpp"
#include "presenter/home_live.hpp"

#include <map>
#include <mutex>
#include <set>
#include <unordered_set>

typedef brls::Event<std::string> UpdateSearchEvent;

namespace brls {
class Label;
class Box;
};
class RecyclingGrid;
class CustomButton;

class HomeLive : public AttachedView, public HomeLiveRequest {
public:
    HomeLive();

    void onLiveList(tsvitch::LiveM3u8ListResult result, bool firstLoad) override;

    ~HomeLive() override;

    void onCreate() override;

    void onError(const std::string &error) override;

    void onShow() override;

    void search();

    void cancelSearch();

    void toggleFavorite();

    void downloadVideo();

    void selectGroupIndex(size_t index);

    // Shows the initial hub with the 3 cards (Live TV / Movies / Series)
    void showContentHub();

    // Enters an Xtream content type (0 = Live TV, 1 = Movies/VOD, 2 = Series),
    // hides the hub, shows the category sidebar + grid and loads the content.
    // groupIndex selects a category (-1 = the default one for the type)
    void enterContentType(int contentType, int groupIndex = -1);

    // Opens the episodes of a series (seasons in the sidebar, episodes in the grid)
    void openSeriesEpisodes(const tsvitch::LiveM3u8& series);

    // Updates the search/refresh button labels according to the active content type
    void updateActionLabels();

    // Clears the cache of the current view and reloads it from the server
    void refreshCurrent();

    // Shows a category's channels, asking for the parental PIN first if it is locked
    void selectGroupContent(const std::string& group);

    // Loads an Xtream content type: memory cache, then the list saved on the SD card, then the server
    void loadXtreamContent(int contentType, bool forceNetwork);

    // Downloads a content type; a background refresh only updates the caches
    void fetchXtreamContent(int contentType, int serial, bool background);

    // Requests the episodes of the series currently shown
    void requestEpisodes();

    // Downloads, one after the other, the lists that were never saved, so entering them later is instant
    void prefetchMissingLists();

    // Leaves the search, the episodes or the content type; false when there is nothing to go back to
    bool goBack();

    // Opens the PIN prompt; calls onUnlock on the correct PIN
    void promptCategoryPin(const std::string& category, std::function<void()> onUnlock);

    void filter(const std::string &key);

    void setSearchCallback(UpdateSearchEvent *event);

    static View *create();

    // The hub cards in the hub, otherwise the list: never a view of the hidden half of the screen
    brls::View *getDefaultFocus() override;

private:
    // Category names in display order, including the virtual "Recently added" / "All" groups
    std::vector<std::string> buildGroupTitles();

    // Items of a category (virtual groups included), leaving out categories locked by the PIN
    tsvitch::LiveM3u8ListResult itemsForGroup(const std::string &group) const;

    // Moves the focus to the first item (or to a visible control while nothing is loaded)
    void focusContent();

    // "All" / "Recently added": groups that mix categories
    bool isVirtualGroup(const std::string &group) const;

    // Movies and series: big posters in 5 columns; live TV keeps the channel cards
    void applyGridLayout();

    // Orders movies/series by the chosen sort mode (server order, recently added, rating, name, year)
    void sortItems(tsvitch::LiveM3u8ListResult &items) const;

    void pickSort();

    void updateSortLabel();

    // Shows the current category (or search) again, e.g. after the sort mode changed
    void refreshVisibleItems();

    int selectedGroupIndex = 0;
    bool isSearchActive    = false;
    bool isInitialLoadInProgress = false;
    bool isXtreamMode      = false;  // true when IPTV_MODE == Xtream
    bool inHubMode         = false;  // true when the 3-card hub is visible
    bool inSeriesEpisodes  = false;  // true when showing a series' episodes
    int  currentLoadType   = 0;      // content type being shown (0=Live, 1=Movies, 2=Series)
    std::string currentSeriesId;     // series_id of the episodes currently shown (for the cache)
    std::string currentSeriesTitle;  // title of the series currently shown
    std::string currentSeriesLogo;   // cover of the series currently shown (fallback for episode stills)
    int seriesListGroupIndex = 0;    // category selected in the series list, restored when leaving the episodes
    int xtreamLoadSerial     = 0;    // identifies the latest load, so late results of older loads are ignored
    bool focusGridOnNextList = false;  // move the focus to the grid when the next list is shown
    bool hasAddedDates       = false;  // the current list has "added" dates (enables "Recently added")
    std::unordered_set<std::string> hiddenGroups;  // categories locked by the PIN in the current list
    std::vector<std::string> currentGroups;         // categories of the current list, in display order
    std::string restoreFocusId;                     // item to focus when the next list is shown (back from episodes)
    std::string lastSearch;                         // text of the current search (to sort its results again)
    int sortMode = 0;                               // see sortItems()
    std::set<int> fetchingTypes;                    // content types being downloaded right now
    int waitingType   = -1;                         // content type the screen waits for (already downloading)
    int waitingSerial = 0;
    bool prefetchStarted = false;
    int sortActionId     = -1;                      // L = sort, only while movies/series are shown

    // In-memory caches so going back does not refetch from the server
    std::map<int, tsvitch::LiveM3u8ListResult> contentCache;          // per content type
    std::map<std::string, tsvitch::LiveM3u8ListResult> episodesCache; // per series_id
    std::set<std::string> unlockedCategories;                        // categories unlocked this session
    tsvitch::LiveM3u8ListResult channelsList;
    std::shared_ptr<std::atomic<bool>> validityFlag;
    brls::Event<>::Subscription exitEventSubscription;
    bool hasExitSubscription = false;
    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/live/recyclingGrid");
    BRLS_BIND(RecyclingGrid, upRecyclingGrid, "dynamic/up/recyclingGrid");
    BRLS_BIND(CustomButton, searchField, "home/search");
    BRLS_BIND(brls::Label, searchLabel, "home/search/label");
    BRLS_BIND(CustomButton, sortButton, "home/sort");
    BRLS_BIND(brls::Label, sortLabel, "home/sort/label");
    BRLS_BIND(CustomButton, refreshButton, "home/refresh");
    BRLS_BIND(brls::Label, refreshLabel, "home/refresh/label");
    BRLS_BIND(brls::Box, leftColumn, "xtream/left/column");
    BRLS_BIND(brls::Box, contentHub, "xtream/hub");
    BRLS_BIND(CustomButton, hubLive, "xtream/hub/live");
    BRLS_BIND(CustomButton, hubMovies, "xtream/hub/movies");
    BRLS_BIND(CustomButton, hubSeries, "xtream/hub/series");
    BRLS_BIND(CustomButton, backButton, "xtream/content/back");
    BRLS_BIND(brls::Label, backLabel, "xtream/content/back/label");
};