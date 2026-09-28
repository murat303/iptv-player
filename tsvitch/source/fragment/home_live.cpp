#include <utility>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cpr/async.h>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/tab_frame.hpp>

#include "fragment/home_live.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/grid_dropdown.hpp"
#include "utils/image_helper.hpp"
#include "utils/activity_helper.hpp"
#include "activity/live_player_activity.hpp"
#include "view/custom_button.hpp"

#include "core/HistoryManager.hpp"
#include "core/FavoriteManager.hpp"
#include "core/ChannelManager.hpp"
#include "core/DownloadManager.hpp"
#include "utils/stream_helper.hpp"

#include "utils/config_helper.hpp"
#include "utils/text_fold.hpp"
#include "utils/video_download.hpp"
#include "utils/xtream_account.hpp"
#include "core/XtreamStore.hpp"
#include "core/Catalog.hpp"
#include "api/tmdb.hpp"
#include "utils/discover.hpp"
#include "view/discover_views.hpp"
#include "tsvitch.h"

#include <borealis/core/box.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/views/label.hpp>

using namespace brls::literals;

// Sentinel scheme for series items (must match the one used in the API)
static const std::string XTREAM_SERIES_SCHEME = "xtream-series://";
// Handler invoked when a series is selected in the list (opens its episodes).
// Active only while the series list is visible; null in every other mode.
static std::function<void(const tsvitch::LiveM3u8&)> g_xtreamSeriesHandler = nullptr;

class DynamicGroupChannels : public RecyclingGridItem {
public:
    explicit DynamicGroupChannels(const std::string& xml) {
        this->inflateFromXMLRes(xml);
        auto theme    = brls::Application::getTheme();
        selectedColor = theme.getColor("color/tsvitch");
        fontColor     = theme.getColor("brls/text");
    }

    void setTitle(const std::string& title) { this->labelTitle->setText(title); }

    void setSelected(bool selected) { this->labelTitle->setTextColor(selected ? selectedColor : fontColor); }

    void prepareForReuse() override {
        this->labelTitle->setText("");
        this->labelTitle->setTextColor(fontColor);
    }

    void cacheForReuse() override {}

    static RecyclingGridItem* create(const std::string& xml = "xml/views/group_channel_dynamic.xml") {
        return new DynamicGroupChannels(xml);
    }

private:
    BRLS_BIND(brls::Label, labelTitle, "title");
    NVGcolor selectedColor{};
    NVGcolor fontColor{};
};

class DataSourceUpList : public RecyclingGridDataSource {
public:
    using OnGroupSelected = std::function<void(const std::string&)>;
    explicit DataSourceUpList(std::vector<std::string> result, OnGroupSelected cb = nullptr)
        : list(std::move(result)), onGroupSelected(cb) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        DynamicGroupChannels* item = (DynamicGroupChannels*)recycler->dequeueReusableCell("Cell");
        item->setTitle(this->list[index]);
        item->setSelected(index == selectedIndex);  // Imposta sempre la selezione!
        return item;
    }

    size_t getItemCount() override { return list.size(); }

    // Highlights a row without selecting it (the caller shows its content)
    void markSelected(size_t index) { selectedIndex = index; }

    void setSelectedIndex(RecyclingGrid* recycler, size_t index) {
        brls::Logger::debug("setSelectedIndex: {}", index);
        if (index >= list.size()) return;
        selectedIndex = index;
        auto* item    = dynamic_cast<DynamicGroupChannels*>(recycler->getGridItemByIndex(index));
        if (!item) return;
        item->setSelected(true);

        // Salva l'indice selezionato
        ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, static_cast<int>(index));

        if (onGroupSelected) onGroupSelected(list[index]);
    }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        brls::Logger::debug("onItemSelected: {}", index);
        std::vector<RecyclingGridItem*>& items = recycler->getGridItems();
        for (auto& i : items) {
            auto* cell = dynamic_cast<DynamicGroupChannels*>(i);
            if (cell) cell->setSelected(false);
        }

        selectedIndex = index;

        auto* item = dynamic_cast<DynamicGroupChannels*>(recycler->getGridItemByIndex(index));
        if (!item) return;
        item->setSelected(true);

        // Salva l'indice selezionato
        ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, static_cast<int>(index));

        if (onGroupSelected) onGroupSelected(list[index]);
    }

    void appendData(const std::vector<std::string>& data) {
        this->list.insert(this->list.end(), data.begin(), data.end());
    }

    void clearData() override { this->list.clear(); }

    const std::string& getGroupNameByIndex(size_t index) const {
        static std::string empty;
        if (index < list.size()) return list[index];
        return empty;
    }

private:
    std::vector<std::string> list;
    size_t selectedIndex = -1;
    OnGroupSelected onGroupSelected;
};

const std::string GridMainAreaCellContentXML = R"xml(
<brls:Box
        width="auto"
        height="@style/brls/sidebar/item_height"
        focusable="true"
        paddingTop="12.5"
        paddingBottom="12.5"
        alignItems="center">

    <brls:Image
        id="area/avatar"
        scalingType="fill"
        cornerRadius="4"
        marginLeft="10"
        marginRight="10"
        width="40"
        height="40"/>

    <brls:Label
            id="area/title"
            width="auto"
            height="auto"
            grow="1"
            fontSize="22" />

</brls:Box>
)xml";

class GridMainAreaCell : public RecyclingGridItem {
public:
    GridMainAreaCell() { this->inflateFromXMLString(GridMainAreaCellContentXML); }

    void setData(const std::string& name, const std::string& pic) {
        this->title->setText(name);
        this->title->setTextColor(fontColor);

        if (pic.empty()) {
            this->image->setImageFromRes("pictures/22_open.png");
        } else {
            ImageHelper::with(image)->load(pic + ImageHelper::face_ext);
        }
    }

    void setSelected(bool value) { this->title->setTextColor(value ? selectedColor : fontColor); }

    void prepareForReuse() override {
        this->image->setImageFromRes("pictures/video-card-bg.png");
        this->title->setText("");
        this->title->setTextColor(fontColor);
    }

    void cacheForReuse() override { ImageHelper::clear(this->image); }

    static RecyclingGridItem* create() { return new GridMainAreaCell(); }

protected:
    BRLS_BIND(brls::Label, title, "area/title");
    BRLS_BIND(brls::Image, image, "area/avatar");

    NVGcolor selectedColor = brls::Application::getTheme().getColor("color/tsvitch");
    NVGcolor fontColor     = brls::Application::getTheme().getColor("brls/text");
};

class DataSourceLiveVideoList : public RecyclingGridDataSource {
public:
    // showGroup: category tag under each channel (for lists that mix categories: "All", search results)
    explicit DataSourceLiveVideoList(tsvitch::LiveM3u8ListResult result, bool showGroup = true)
        : videoList(std::move(result)), showGroup(showGroup) {}
    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        tsvitch::LiveM3u8& r = this->videoList[index];
        // Movies and series use the big poster card
        bool poster = r.type == 1 || r.type == 2;
        auto* item  = (RecyclingGridItemLiveVideoCard*)recycler->dequeueReusableCell(poster ? "Poster" : "Cell");
        item->setChannel(r, showGroup);
        return item;
    }

    size_t getItemCount() override { return videoList.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        const tsvitch::LiveM3u8& item = videoList[index];
        // Movies and series open their information screen (series items carry a sentinel url)
        if (item.type == 1 || item.url.rfind(XTREAM_SERIES_SCHEME, 0) == 0) {
            Intent::openXtreamDetail(item, [recycler]() { RecyclingGridItemLiveVideoCard::refreshWatchStates(recycler); });
            return;
        }
        // Não registra no histórico conteúdo de categoria adulta (privacidade)
        if (!ProgramConfig::instance().isAdultCategory(item.groupTitle)) {
            HistoryManager::get()->add(item);
        }
        Intent::openLive(videoList, index, [recycler]() { recycler->reloadWithFocus(LiveActivity::lastPlayedIndex()); });
    }

    void appendData(const tsvitch::LiveM3u8ListResult& data) {
        this->videoList.insert(this->videoList.end(), data.begin(), data.end());
    }

    void clearData() override { this->videoList.clear(); }

private:
    tsvitch::LiveM3u8ListResult videoList;
    bool showGroup = true;
};

static const std::string UNCATEGORIZED = "Uncategorized";
static constexpr size_t RECENT_LIMIT   = 300;  // items in the virtual "Recently added" category

static const std::string& groupOf(const tsvitch::LiveM3u8& item) {
    return item.groupTitle.empty() ? UNCATEGORIZED : item.groupTitle;
}

static std::string allGroupLabel() { return "tsvitch/xtream/group/all"_i18n; }
static std::string recentGroupLabel() { return "tsvitch/xtream/group/recent"_i18n; }
static std::string genresGroupLabel() { return "tsvitch/xtream/group/genres"_i18n; }

/// The genre tiles of the movie or series list: a tile opens every title of that genre
class DataSourceGenreTiles : public RecyclingGridDataSource {
public:
    explicit DataSourceGenreTiles(std::vector<tsvitch::discover::Collection> tiles) : tiles(std::move(tiles)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell = (GenreGridCell*)recycler->dequeueReusableCell("Genre");
        cell->setGenre(tiles[index]);
        return cell;
    }

    size_t getItemCount() override { return tiles.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        Intent::openDiscoverList(tiles[index].id, tiles[index].title);
    }

    void clearData() override { tiles.clear(); }

private:
    std::vector<tsvitch::discover::Collection> tiles;
};

namespace {
// The HomeLive that receives the list download states (one home screen at a time)
HomeLive* loadObserverOwner = nullptr;
}  // namespace

HomeLive::HomeLive() {
    loadObserverOwner = this;
    CLIENT::setXtreamLoadObserver([this](const tsvitch::XtreamLoadState& state) { this->onXtreamLoad(state); });
    this->inflateFromXMLRes("xml/fragment/home_live.xml");
    brls::Logger::info("Fragment HomeLive: constructor called");

    // Inizializza il flag di validità
    validityFlag = std::make_shared<std::atomic<bool>>(true);

    // Sottoscrivi all'evento di uscita per cancellare tutti i task asincroni
    exitEventSubscription = brls::Application::getExitEvent()->subscribe([this]() {
        brls::Logger::info("HomeLive: Exit event received, canceling all async operations");
        if (validityFlag) {
            validityFlag->store(false);
        }
    });
    hasExitSubscription = true;

    recyclingGrid->registerCell("Cell", []() { return RecyclingGridItemLiveVideoCard::create(); });
    recyclingGrid->registerCell("Poster", []() { return RecyclingGridItemLiveVideoCard::createPoster(); });
    recyclingGrid->registerCell("Genre", []() { return GenreGridCell::create(); });

    // The genre tiles wait for the catalogue when it was not read yet
    auto genresValid    = validityFlag;
    catalogSubscription = Catalog::instance().getChangedEvent()->subscribe([this, genresValid]() {
        if (genresValid->load() && waitingGenres) this->showGenreTiles();
    });

    upRecyclingGrid->registerCell("Cell", []() { return DynamicGroupChannels::create(); });

    isXtreamMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 1;

    // Hub cards (Live TV / Movies / Series) and back button — Xtream mode only
    hubLive->registerClickAction([this](brls::View*) { this->enterContentType(0); return true; });
    hubMovies->registerClickAction([this](brls::View*) { this->enterContentType(1); return true; });
    hubSeries->registerClickAction([this](brls::View*) { this->enterContentType(2); return true; });
    backButton->registerClickAction([this](brls::View*) { this->goBack(); return true; });

    // Enable touch/tap on the cards and the back button (gamepad click alone is not enough)
    hubLive->addGestureRecognizer(new brls::TapGestureRecognizer(hubLive));
    hubMovies->addGestureRecognizer(new brls::TapGestureRecognizer(hubMovies));
    hubSeries->addGestureRecognizer(new brls::TapGestureRecognizer(hubSeries));
    backButton->addGestureRecognizer(new brls::TapGestureRecognizer(backButton));

    // Botão de atualizar (limpa o cache do modo atual e recarrega do servidor)
    refreshButton->registerClickAction([this](brls::View*) { this->refreshCurrent(); return true; });
    refreshButton->addGestureRecognizer(new brls::TapGestureRecognizer(refreshButton));

    // Sort button (movies and series)
    sortMode = ProgramConfig::instance().getSettingItem(SettingItem::XTREAM_SORT_MODE, 0);
    sortButton->registerClickAction([this](brls::View*) { this->pickSort(); return true; });
    sortButton->addGestureRecognizer(new brls::TapGestureRecognizer(sortButton));

    // The search box opens the keyboard (Y does the same)
    searchField->registerClickAction([this](brls::View*) { this->search(); return true; });
    searchField->addGestureRecognizer(new brls::TapGestureRecognizer(searchField));

    // Source reload: in Xtream go back to the hub, in M3U8 reload directly
    auto reloadOnSourceChange = [this]() {
        isXtreamMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 1;
        ChannelManager::get()->remove();
        // The lists of the previous account/source are no longer valid
        contentCache.clear();
        episodesCache.clear();
        XtreamStore::clear();
        Catalog::instance().reset();
        if (isXtreamMode) {
            this->showContentHub();
        } else {
            brls::Threading::sync([this]() {
                recyclingGrid->showSkeleton();
                upRecyclingGrid->setVisibility(brls::Visibility::GONE);
            });
            this->requestLiveList();
            this->selectGroupIndex(0);
        }
    };
    OnM3U8UrlChanged.subscribe(reloadOnSourceChange);
    OnIPTVModeChanged.subscribe(reloadOnSourceChange);
    OnXtreamChanged.subscribe([reloadOnSourceChange](const XtreamData&) { reloadOnSourceChange(); });

    if (isXtreamMode) {
        // In Xtream mode, entry shows the 3-card hub (nothing loads until a choice is made)
        brls::Logger::info("HomeLive constructor: Xtream mode -> showing content hub");
        this->showContentHub();
        isInitialLoadInProgress = false;
        // Lists never downloaded before are fetched while the user looks at the hub
        auto isValid = validityFlag;
        brls::delay(3000, [this, isValid]() {
            if (isValid->load()) this->prefetchMissingLists();
        });
        // Reminder before the subscription ends (the server is asked at most once a day)
        brls::delay(6000, []() { tsvitch::checkXtreamExpiry(); });
    } else {
        // M3U8: skeleton + smart cache (original behavior)
        brls::Logger::debug("HomeLive constructor: M3U8 mode, using intelligent caching");
        recyclingGrid->showSkeleton();
        upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        isInitialLoadInProgress = true;

        brls::Threading::async([this, validityFlag = this->validityFlag] {
            if (!validityFlag || !validityFlag->load()) return;

            auto cachedChannels = ChannelManager::get()->loadIfValid();
            brls::Logger::info("HomeLive: Smart cache check completed, found {} channels", cachedChannels.size());

            brls::sync([this, cachedChannels, validityFlag]() {
                if (!validityFlag || !validityFlag->load()) return;

                if (!cachedChannels.empty()) {
                    this->onLiveList(cachedChannels, false);
                } else {
                    this->requestLiveList();
                }
                isInitialLoadInProgress = false;
            });
        });
    }
}

void HomeLive::showContentHub() {
    if (!isXtreamMode) return;
    inHubMode      = true;
    isSearchActive = false;
    this->hideLoading();
    // The hub has no list: the list's buttons (search, favorite, download, refresh, sort) do nothing there and
    // show no hint; a list registers them again. They are replaced by button, not removed by id: borealis gives
    // replaced actions the same id, so removing by id could take away the Back button.
    for (auto button : {brls::BUTTON_Y, brls::BUTTON_X, brls::BUTTON_RT, brls::BUTTON_RB, brls::BUTTON_LB})
        this->registerAction("", button, [](brls::View*) { return true; }, true);
    sortActionId = -1;
    for (int type = 0; type <= 2; type++) this->updateHubStatus(type);
    this->registerBackAction();

    leftColumn->setVisibility(brls::Visibility::GONE);
    recyclingGrid->setVisibility(brls::Visibility::GONE);
    searchField->setVisibility(brls::Visibility::GONE);
    refreshButton->setVisibility(brls::Visibility::GONE);
    sortButton->setVisibility(brls::Visibility::GONE);
    backButton->setVisibility(brls::Visibility::GONE);
    contentHub->setVisibility(brls::Visibility::VISIBLE);

    brls::Application::giveFocus(hubLive);
}

void HomeLive::enterContentType(int contentType, int groupIndex) {
    brls::Logger::info("HomeLive: entering Xtream content type {}", contentType);
    this->hideLoading();
    this->registerBackAction();
    inHubMode        = false;
    inSeriesEpisodes = false;
    ProgramConfig::instance().setXtreamContentType(contentType);

    // The series handler is only active while the series list is visible
    if (contentType == 2) {
        g_xtreamSeriesHandler = [this](const tsvitch::LiveM3u8& series) { this->openSeriesEpisodes(series); };
    } else {
        g_xtreamSeriesHandler = nullptr;
    }

    currentLoadType = contentType;

    std::string labelKey = contentType == 2   ? "tsvitch/xtream/content/series"
                           : contentType == 1 ? "tsvitch/xtream/content/movies"
                                              : "tsvitch/xtream/content/live";
    backLabel->setText(brls::getStr(labelKey));
    updateActionLabels();

    contentHub->setVisibility(brls::Visibility::GONE);
    leftColumn->setVisibility(brls::Visibility::VISIBLE);
    recyclingGrid->setVisibility(brls::Visibility::VISIBLE);
    searchField->setVisibility(brls::Visibility::VISIBLE);
    refreshButton->setVisibility(brls::Visibility::VISIBLE);
    sortButton->setVisibility(contentType == 0 ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    backButton->setVisibility(brls::Visibility::VISIBLE);
    this->updateSortLabel();

    // Live TV opens its first real category (after "All"); movies and series open "Recently added"
    int defaultIndex = contentType == 0 ? 1 : 0;
    ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX,
                                             groupIndex >= 0 ? groupIndex : defaultIndex);
    channelsList.clear();
    isSearchActive      = false;
    selectedGroupIndex  = 0;
    focusGridOnNextList = true;

    // The hub card that had the focus is hidden now: keep the focus on a visible control
    // until the list is shown (the empty grid cannot take it)
    brls::Application::giveFocus(backButton);
    this->loadXtreamContent(contentType, false);
}

void HomeLive::loadXtreamContent(int contentType, bool forceNetwork) {
    int serial = ++xtreamLoadSerial;
    // A download the user started (first load, refresh) still runs: wait for it with the card instead of
    // showing the older list from the SD card
    if (!forceNetwork && fetchingTypes.count(contentType) && foregroundFetches.count(contentType)) {
        recyclingGrid->showSkeleton();
        upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        this->fetchXtreamContent(contentType, serial, false);
        return;
    }
    if (!forceNetwork) {
        auto cached = contentCache.find(contentType);
        if (cached != contentCache.end() && !cached->second.empty()) {
            brls::Logger::info("HomeLive: content type {} from memory ({} items)", contentType, cached->second.size());
            this->onLiveList(cached->second, false);
            return;
        }
    }

    recyclingGrid->showSkeleton();
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    if (forceNetwork) {
        this->fetchXtreamContent(contentType, serial, false);
        return;
    }

    // The list saved on the SD card is read off the UI thread
    auto isValid = validityFlag;
    cpr::async([this, contentType, serial, isValid]() {
        auto list       = std::make_shared<tsvitch::LiveM3u8ListResult>();
        int64_t savedAt = 0;
        bool found      = XtreamStore::load(contentType, *list, savedAt);
        bool upgrade    = found && XtreamStore::needsUpgrade(contentType);
        brls::sync([this, contentType, serial, isValid, list, found, savedAt, upgrade]() {
            if (!isValid->load() || serial != xtreamLoadSerial) return;
            if (!found || list->empty()) {
                this->fetchXtreamContent(contentType, serial, false);
                return;
            }
            this->onLiveList(std::move(*list), false);
            // An old list stays usable while a fresh one is downloaded for the next visit (a list saved by an older
            // version too: it has no TMDB ids for the discovery screen), unless lists refresh only by hand
            bool autoRefresh = ProgramConfig::instance().getSettingItem(SettingItem::XTREAM_AUTO_REFRESH, 0) != 2;
            if (XtreamStore::isStale(savedAt) || (upgrade && autoRefresh))
                this->fetchXtreamContent(contentType, serial, true);
        });
    });
}

void HomeLive::fetchXtreamContent(int contentType, int serial, bool background) {
    if (fetchingTypes.count(contentType)) {
        // Already downloading (prefetch or refresh): the screen shows that result when it arrives
        if (!background) {
            waitingType   = contentType;
            waitingSerial = serial;
            foregroundFetches.insert(contentType);
            this->showLoading(contentType);
        }
        return;
    }
    fetchingTypes.insert(contentType);
    failedTypes.erase(contentType);
    lastLoadState.erase(contentType);
    if (!background) {
        foregroundFetches.insert(contentType);
        this->showLoading(contentType);
    }
    this->updateHubStatus(contentType);

    auto isValid = validityFlag;
    // Whether the screen still shows this content type and asked for this download (or waits for it)
    auto wanted = [this, contentType, serial, background]() {
        bool waited = waitingType == contentType && waitingSerial == xtreamLoadSerial;
        if (waited) waitingType = -1;
        bool onScreen = currentLoadType == contentType && !inHubMode && !inSeriesEpisodes;
        return onScreen && ((serial == xtreamLoadSerial && !background) || waited);
    };
    auto onDone = [this, contentType, wanted, isValid](tsvitch::LiveM3u8ListResult result) {
        if (!isValid->load()) return;
        fetchingTypes.erase(contentType);
        foregroundFetches.erase(contentType);
        lastLoadState.erase(contentType);
        if (loadingType == contentType) this->hideLoading();
        if (!result.empty()) {
            auto toSave = std::make_shared<tsvitch::LiveM3u8ListResult>(result);
            cpr::async([contentType, toSave]() { XtreamStore::save(contentType, *toSave); });
            // The discovery screen uses the new movies and series too
            Catalog::instance().update(contentType, toSave);
        }
        if (!wanted()) {
            // Not on screen (or a background download): keep it for the next visit
            if (!result.empty()) contentCache[contentType] = std::move(result);
            this->updateHubStatus(contentType);
            return;
        }
        this->onLiveList(std::move(result), false);
        this->updateHubStatus(contentType);
    };
    auto onFail = [this, contentType, wanted, isValid](const std::string& error, int) {
        if (!isValid->load()) return;
        fetchingTypes.erase(contentType);
        foregroundFetches.erase(contentType);
        lastLoadState.erase(contentType);
        failedTypes.insert(contentType);
        if (loadingType == contentType) this->hideLoading();
        this->updateHubStatus(contentType);
        if (wanted()) this->onError(error);
    };

    if (contentType == 2)
        CLIENT::get_xtream_series(onDone, onFail);
    else if (contentType == 1)
        CLIENT::get_xtream_vod(onDone, onFail);
    else
        CLIENT::get_xtream_channels(onDone, onFail);
}

void HomeLive::showLoading(int contentType) {
    loadingType     = contentType;
    const char* key = contentType == 2   ? "tsvitch/xtream/loading/title/series"
                      : contentType == 1 ? "tsvitch/xtream/loading/title/movies"
                                         : "tsvitch/xtream/loading/title/live";
    loadingTitle->setText(brls::getStr(key));
    auto known = lastLoadState.find(contentType);
    if (known != lastLoadState.end()) {
        this->applyLoadState(known->second);
    } else {
        loadingDetail->setText("tsvitch/xtream/loading/connecting"_i18n);
        loadingBar->setProgress(-1);
        loadingBar->setVisibility(brls::Visibility::VISIBLE);
    }
    loadingBox->setVisibility(brls::Visibility::VISIBLE);
}

void HomeLive::hideLoading() {
    loadingType = -1;
    loadingBox->setVisibility(brls::Visibility::GONE);
}

void HomeLive::onXtreamLoad(const tsvitch::XtreamLoadState& state) {
    // Remembered also for lists downloading in the background: opening one shows where it is
    lastLoadState[state.contentType] = state;
    if (state.phase == tsvitch::XtreamLoadState::PREPARING && state.bytes > 0) {
        XtreamStore::rememberDownloadSize(state.contentType, state.bytes);
        sizeEstimates[state.contentType] = state.bytes;
    }
    if (state.contentType == loadingType) this->applyLoadState(state);
    this->updateHubStatus(state.contentType);
}

std::string HomeLive::downloadText(int contentType, const tsvitch::XtreamLoadState& state, float& fraction) {
    double received = state.bytes / 1048576.0;
    int64_t total   = state.total;
    bool estimated  = false;
    if (total <= 0) {
        auto known = sizeEstimates.find(contentType);
        if (known == sizeEstimates.end())
            known = sizeEstimates.emplace(contentType, XtreamStore::lastDownloadSize(contentType)).first;
        total     = known->second;
        estimated = true;
    }
    // Nothing tells the size (first download from a server that does not say it), or the list grew
    if (total <= 0 || (estimated && state.bytes > total)) {
        fraction = -1;
        return fmt::format("{:.1f} MB", received);
    }
    fraction = std::min(estimated ? 0.99f : 1.0f, static_cast<float>(state.bytes) / static_cast<float>(total));
    return fmt::format(estimated ? "{:.1f} / ~{:.1f} MB" : "{:.1f} / {:.1f} MB", received, total / 1048576.0);
}

namespace {
// 38305 -> "38,305" in English, "38.305" in the other languages of the app
std::string groupDigits(size_t number) {
    std::string digits = std::to_string(number);
    std::string separator = brls::Application::getLocale() == brls::LOCALE_EN_US ? "," : ".";
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(i, separator);
    return digits;
}
}  // namespace

void HomeLive::updateHubStatus(int contentType) {
    if (!isXtreamMode) return;
    brls::Label* label = contentType == 2 ? hubSeriesStatus : contentType == 1 ? hubMoviesStatus : hubLiveStatus;
    ProgressLine* bar  = contentType == 2 ? hubSeriesBar : contentType == 1 ? hubMoviesBar : hubLiveBar;
    std::string text;
    float fraction = -1;
    bool showBar   = false;
    if (fetchingTypes.count(contentType)) {
        auto known = lastLoadState.find(contentType);
        if (known == lastLoadState.end() || known->second.phase == tsvitch::XtreamLoadState::QUEUED) {
            text = "tsvitch/xtream/hub/waiting"_i18n;
        } else {
            const auto& state = known->second;
            showBar           = true;
            switch (state.phase) {
                case tsvitch::XtreamLoadState::DOWNLOADING:
                    text = state.bytes > 0 ? brls::getStr("tsvitch/xtream/hub/downloading",
                                                          this->downloadText(contentType, state, fraction))
                                           : "tsvitch/xtream/hub/connecting"_i18n;
                    break;
                case tsvitch::XtreamLoadState::RETRY:
                    text    = brls::getStr("tsvitch/xtream/hub/retry", state.retryInSeconds);
                    showBar = false;
                    break;
                case tsvitch::XtreamLoadState::PREPARING:
                    text     = "tsvitch/xtream/hub/preparing"_i18n;
                    fraction = 1;
                    break;
                default:
                    text = "tsvitch/xtream/hub/connecting"_i18n;
                    break;
            }
        }
    } else if (failedTypes.count(contentType)) {
        text = "tsvitch/xtream/hub/failed"_i18n;
    } else {
        size_t count = 0;
        auto cached  = contentCache.find(contentType);
        if (cached != contentCache.end()) {
            count = cached->second.size();
        } else {
            int64_t savedAt = 0;
            uint32_t items  = 0;
            if (XtreamStore::header(contentType, savedAt, items)) count = items;
        }
        if (count > 0) {
            const char* key = contentType == 2   ? "tsvitch/xtream/hub/count/series"
                              : contentType == 1 ? "tsvitch/xtream/hub/count/movies"
                                                 : "tsvitch/xtream/hub/count/live";
            text = brls::getStr(key, groupDigits(count));
        }
    }
    label->setText(text);
    label->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    bar->setProgress(fraction);
    bar->setVisibility(showBar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void HomeLive::applyLoadState(const tsvitch::XtreamLoadState& state) {
    // The bar: the part downloaded when the size is known (or estimated), otherwise a sliding segment
    bool bar       = true;
    float fraction = -1;
    switch (state.phase) {
        case tsvitch::XtreamLoadState::QUEUED:
            loadingDetail->setText("tsvitch/xtream/loading/queued"_i18n);
            bar = false;
            break;
        case tsvitch::XtreamLoadState::CATEGORIES:
            loadingDetail->setText("tsvitch/xtream/loading/categories"_i18n);
            break;
        case tsvitch::XtreamLoadState::DOWNLOADING:
            if (state.bytes <= 0)
                loadingDetail->setText("tsvitch/xtream/loading/connecting"_i18n);
            else
                loadingDetail->setText(brls::getStr("tsvitch/xtream/loading/downloading",
                                                    this->downloadText(state.contentType, state, fraction)));
            break;
        case tsvitch::XtreamLoadState::RETRY:
            loadingDetail->setText(
                brls::getStr("tsvitch/xtream/loading/retry", state.retryInSeconds, state.attempt, state.attempts));
            bar = false;
            break;
        case tsvitch::XtreamLoadState::PREPARING:
            loadingDetail->setText("tsvitch/xtream/loading/preparing"_i18n);
            fraction = 1;
            break;
    }
    loadingBar->setProgress(fraction);
    loadingBar->setVisibility(bar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void HomeLive::registerBackAction() {
    this->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        if (this->goBack()) return true;

        // A running download is paused when the app closes; it continues from the downloads screen later
        DownloadItem active;
        if (DownloadManager::instance().getActiveDownload(active)) {
            auto dialog = new brls::Dialog("tsvitch/download/exit_warning"_i18n);
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->addButton("hints/ok"_i18n, []() { brls::Application::quit(); });
            dialog->open();
        } else {
            auto dialog = new brls::Dialog("hints/exit_hint"_i18n);
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->addButton("hints/ok"_i18n, []() { brls::Application::quit(); });
            dialog->open();
        }
        return true;
    });
}

void HomeLive::prefetchMissingLists() {
    if (prefetchStarted || !isXtreamMode) return;
    prefetchStarted = true;
    // Movie and series lists saved by an older version are downloaded again once: the discovery screen needs
    // their TMDB ids (not when lists refresh only by hand)
    bool autoRefresh = ProgramConfig::instance().getSettingItem(SettingItem::XTREAM_AUTO_REFRESH, 0) != 2;
    // Small lists first; the server gets one request at a time anyway
    for (int type : {0, 2, 1}) {
        bool missing = !contentCache.count(type) && !XtreamStore::exists(type);
        bool upgrade = autoRefresh && XtreamStore::needsUpgrade(type);
        if (!missing && !upgrade) continue;
        brls::Logger::info("HomeLive: downloading list type {} in the background ({})", type,
                           missing ? "never saved" : "older format");
        this->fetchXtreamContent(type, 0, true);
    }
}

void HomeLive::updateActionLabels() {
    if (!isXtreamMode) return;
    // Whole phrases per type ("Search movies" / "Film ara"): word order differs between languages
    const char* type = currentLoadType == 2 ? "series" : currentLoadType == 1 ? "movies" : "live";
    searchLabel->setText(brls::getStr(std::string("tsvitch/xtream/search_label/") + type));
    refreshLabel->setText(brls::getStr(std::string("tsvitch/xtream/refresh_label/") + type));
}

void HomeLive::refreshCurrent() {
    if (inHubMode) return;
    brls::Logger::info("HomeLive: refresh requested (episodes={}, type={})", inSeriesEpisodes, currentLoadType);
    isSearchActive      = false;
    focusGridOnNextList = true;

    if (inSeriesEpisodes) {
        episodesCache.erase(currentSeriesId);
        this->requestEpisodes();
        return;
    }

    if (isXtreamMode) {
        contentCache.erase(currentLoadType);
        this->loadXtreamContent(currentLoadType, true);
        return;
    }

    recyclingGrid->showSkeleton();
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    channelsList.clear();
    ChannelManager::get()->remove();
    this->requestLiveList();
}

void HomeLive::openSeriesEpisodes(const tsvitch::LiveM3u8& series) {
    brls::Logger::info("HomeLive: opening episodes for series '{}' (id={})", series.title, series.id);

    // In the episodes view items are playable: disable the series handler
    g_xtreamSeriesHandler = nullptr;
    // The category of the series list is restored when going back
    seriesListGroupIndex = ProgramConfig::instance().getSettingItem(SettingItem::GROUP_SELECTED_INDEX, 0);
    ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, 0);
    isSearchActive      = false;
    selectedGroupIndex  = 0;
    currentSeriesId     = series.id;
    currentSeriesTitle  = series.title;
    currentSeriesLogo   = series.logo;
    focusGridOnNextList = true;
    // Marca já como "em episódios" para que Voltar durante o carregamento
    // retorne à lista de séries (e não ao hub)
    inSeriesEpisodes = true;
    backLabel->setText(series.title);

    // Cache hit: episódios já carregados desta série
    auto cachedEps = episodesCache.find(series.id);
    if (cachedEps != episodesCache.end() && !cachedEps->second.empty()) {
        brls::Logger::info("HomeLive: episodes for series {} served from cache ({} items)", series.id,
                           cachedEps->second.size());
        this->onLiveList(cachedEps->second, false);
        return;
    }
    this->requestEpisodes();
}

void HomeLive::requestEpisodes() {
    recyclingGrid->showSkeleton();
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    // The focused card is gone while loading
    brls::Application::giveFocus(backButton);

    std::string seriesId = currentSeriesId;
    auto isValid         = validityFlag;
    CLIENT::get_xtream_series_info(
        seriesId,
        [this, seriesId, isValid](tsvitch::LiveM3u8ListResult episodes) {
            if (!isValid || !isValid->load()) return;
            // Se o usuário já voltou/trocou de série, ignora este resultado tardio
            if (!inSeriesEpisodes || currentSeriesId != seriesId) return;
            if (episodes.empty()) {
                recyclingGrid->setEmpty();
                upRecyclingGrid->setVisibility(brls::Visibility::GONE);
                return;
            }
            // Reuse the group pipeline: seasons become the sidebar groups
            this->onLiveList(std::move(episodes), false);
        },
        [this, seriesId, isValid](const std::string& error, int) {
            if (!isValid || !isValid->load()) return;
            if (!inSeriesEpisodes || currentSeriesId != seriesId) return;
            this->onError(error);
        },
        currentSeriesLogo);
}

bool HomeLive::goBack() {
    if (isSearchActive) {
        this->cancelSearch();
    } else if (inSeriesEpisodes) {
        // From the episodes, back returns to the series list, on the series that was open
        restoreFocusId = currentSeriesId;
        this->enterContentType(2, seriesListGroupIndex);
    } else if (isXtreamMode && !inHubMode) {
        // In Xtream, back returns to the 3-card hub instead of quitting the app
        this->showContentHub();
    } else {
        return false;
    }
    return true;
}

void HomeLive::onError(const std::string& error) {
    brls::Logger::error("Fragment HomeLive: onError: {}", error);
    brls::sync([this, error]() {
        this->recyclingGrid->setError(error);
        this->upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    });

    //dialog to show error
    auto dialog = new brls::Dialog("hints/network_error"_i18n);
    dialog->addButton("hints/back"_i18n, []() {});
    dialog->open();
}

std::vector<std::string> HomeLive::buildGroupTitles() {
    // Categories in the order of the list (the server's order in Xtream)
    std::vector<std::string> titles;
    std::unordered_set<std::string> seen;
    hasAddedDates = false;
    for (const auto& item : this->channelsList) {
        if (seen.insert(groupOf(item)).second) titles.push_back(groupOf(item));
        if (item.added > 0) hasAddedDates = true;
    }
    // M3U8 keeps its alphabetical order
    if (!isXtreamMode) std::sort(titles.begin(), titles.end());

    // The PIN check is done once per category, not once per item
    hiddenGroups.clear();
    for (const auto& title : titles) {
        if (ProgramConfig::instance().isCategoryLocked(title) && !unlockedCategories.count(title))
            hiddenGroups.insert(title);
    }

    if (isXtreamMode && !inSeriesEpisodes) {
        ProgramConfig::instance().addKnownCategories(titles);
        if (titles.size() > 1) {
            std::vector<std::string> virtualGroups;
            if (currentLoadType != 0 && hasAddedDates) virtualGroups.push_back(recentGroupLabel());
            virtualGroups.push_back(allGroupLabel());
            // Movies and series: their genres as tiles (Action, Comedy...)
            if (currentLoadType != 0) virtualGroups.push_back(genresGroupLabel());
            titles.insert(titles.begin(), virtualGroups.begin(), virtualGroups.end());
        }
    }
    return titles;
}

tsvitch::LiveM3u8ListResult HomeLive::itemsForGroup(const std::string& group) const {
    tsvitch::LiveM3u8ListResult out;
    if (this->isGenresGroup(group)) return out;
    bool virtualGroups = isXtreamMode && !inSeriesEpisodes;
    if (virtualGroups && group == allGroupLabel()) {
        out.reserve(channelsList.size());
        for (const auto& item : channelsList)
            if (!hiddenGroups.count(groupOf(item))) out.push_back(item);
    } else if (virtualGroups && group == recentGroupLabel()) {
        std::vector<const tsvitch::LiveM3u8*> recent;
        for (const auto& item : channelsList)
            if (item.added > 0 && !hiddenGroups.count(groupOf(item))) recent.push_back(&item);
        size_t count = std::min(RECENT_LIMIT, recent.size());
        std::partial_sort(recent.begin(), recent.begin() + count, recent.end(),
                          [](const tsvitch::LiveM3u8* a, const tsvitch::LiveM3u8* b) { return a->added > b->added; });
        out.reserve(count);
        for (size_t i = 0; i < count; i++) out.push_back(*recent[i]);
    } else {
        for (const auto& item : channelsList)
            if (groupOf(item) == group) out.push_back(item);
    }
    this->sortItems(out);
    return out;
}

void HomeLive::sortItems(tsvitch::LiveM3u8ListResult& items) const {
    if (!isXtreamMode || currentLoadType == 0 || inSeriesEpisodes || sortMode == 0) return;
    auto by = [&items](auto better) { std::stable_sort(items.begin(), items.end(), better); };
    switch (sortMode) {
        case 1:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.added > b.added; });
            break;
        case 2:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.rating > b.rating; });
            break;
        case 3: {
            // A-Z with Turkish letters folded: every title is folded once, not on each comparison
            std::vector<std::pair<std::string, size_t>> keys;
            keys.reserve(items.size());
            for (size_t i = 0; i < items.size(); i++) keys.emplace_back(tsvitch::foldForSearch(items[i].title), i);
            std::stable_sort(keys.begin(), keys.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            tsvitch::LiveM3u8ListResult sorted;
            sorted.reserve(items.size());
            for (const auto& key : keys) sorted.push_back(std::move(items[key.second]));
            items.swap(sorted);
            break;
        }
        case 4:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.year > b.year; });
            break;
        default:
            break;
    }
}

static const char* SORT_KEYS[] = {"tsvitch/xtream/sort/server", "tsvitch/xtream/sort/recent",
                                  "tsvitch/xtream/sort/rating", "tsvitch/xtream/sort/name",
                                  "tsvitch/xtream/sort/year"};

void HomeLive::updateSortLabel() {
    int mode = sortMode >= 0 && sortMode < 5 ? sortMode : 0;
    sortLabel->setText(brls::getStr("tsvitch/xtream/sort/label", brls::getStr(SORT_KEYS[mode])));
}

void HomeLive::pickSort() {
    std::vector<std::string> names;
    for (const char* key : SORT_KEYS) names.push_back(brls::getStr(key));
    auto isValid = validityFlag;
    BaseDropdown::text(
        "tsvitch/xtream/sort/title"_i18n, names,
        [this, isValid](int mode) {
            if (!isValid->load() || mode < 0 || mode == sortMode) return;
            sortMode = mode;
            ProgramConfig::instance().setSettingItem(SettingItem::XTREAM_SORT_MODE, mode);
            this->updateSortLabel();
            this->refreshVisibleItems();
        },
        sortMode);
}

void HomeLive::refreshVisibleItems() {
    if (channelsList.empty()) return;
    if (isSearchActive && !lastSearch.empty()) {
        this->filter(lastSearch);
        return;
    }
    int index = ProgramConfig::instance().getSettingItem(SettingItem::GROUP_SELECTED_INDEX, 0);
    if (index >= 0 && index < (int)currentGroups.size()) this->selectGroupContent(currentGroups[index]);
}

void HomeLive::applyGridLayout() {
    bool posters                      = isXtreamMode && currentLoadType != 0 && !inSeriesEpisodes;
    recyclingGrid->spanCount          = posters ? 5 : 4;
    recyclingGrid->estimatedRowHeight = posters ? 305 : 200;

    // L opens the sort menu where sorting exists
    if (posters && sortActionId < 0) {
        sortActionId = this->registerAction("tsvitch/xtream/sort/title"_i18n, brls::BUTTON_LB, [this](...) {
            this->pickSort();
            return true;
        });
    } else if (!posters && sortActionId >= 0) {
        // No sort for this list: L does nothing and has no hint (replaced, not removed by id; see showContentHub)
        this->registerAction("", brls::BUTTON_LB, [](brls::View*) { return true; }, true);
        sortActionId = -1;
    }
}

brls::View* HomeLive::getDefaultFocus() {
    if (isXtreamMode && inHubMode) return hubLive;
    if (auto* view = recyclingGrid->getDefaultFocus()) return view;
    if (auto* view = upRecyclingGrid->getDefaultFocus()) return view;
    if (backButton->getVisibility() == brls::Visibility::VISIBLE) return backButton;
    return AttachedView::getDefaultFocus();
}

void HomeLive::focusContent() {
    if (recyclingGrid->getDefaultFocus())
        brls::Application::giveFocus(recyclingGrid);
    else if (upRecyclingGrid->getVisibility() == brls::Visibility::VISIBLE && upRecyclingGrid->getDefaultFocus())
        brls::Application::giveFocus(upRecyclingGrid);
    else if (backButton->getVisibility() == brls::Visibility::VISIBLE)
        brls::Application::giveFocus(backButton);
}

void HomeLive::onLiveList(tsvitch::LiveM3u8ListResult result, bool firstLoad) {
    brls::Logger::info("Fragment HomeLive: onLiveList - received {} channels", result.size());
    this->hideLoading();
    if (result.empty()) {
        recyclingGrid->setEmpty();
        upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        return;
    }

    this->registerBackAction();

    this->registerAction("hints/search"_i18n, brls::BUTTON_Y, [this](...) {
        this->search();
        return true;
    });

    this->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](...) {
        this->toggleFavorite();
        return true;
    });

    // Live channels cannot be downloaded: the button only explains that, so it gets no hint there
    this->registerAction(
        "tsvitch/download/action"_i18n, brls::BUTTON_RT,
        [this](...) {
            this->downloadVideo();
            return true;
        },
        isXtreamMode && currentLoadType == 0);

    this->registerAction("tsvitch/xtream/action/refresh"_i18n, brls::BUTTON_RB, [this](...) {
        this->refreshCurrent();
        return true;
    });

    this->channelsList = std::move(result);

    // Guarda em cache (em memória) para não refazer o fetch ao voltar (só no Xtream)
    if (isXtreamMode) {
        if (inSeriesEpisodes && !currentSeriesId.empty()) {
            episodesCache[currentSeriesId] = this->channelsList;
        } else if (!inSeriesEpisodes) {
            contentCache[currentLoadType] = this->channelsList;
        }
    }

    auto isValidFlag = validityFlag;
    brls::sync([this, isValidFlag, firstLoad]() {
        if (!isValidFlag->load() || this->channelsList.empty()) return;

        auto groupTitles    = this->buildGroupTitles();
        this->currentGroups = groupTitles;
        this->applyGridLayout();

        int lastIndex = ProgramConfig::instance().getSettingItem(SettingItem::GROUP_SELECTED_INDEX, 0);
        if (lastIndex < 0 || lastIndex >= (int)groupTitles.size()) lastIndex = 0;
        // Não abrir automaticamente uma categoria bloqueada: escolhe a primeira liberada
        if (hiddenGroups.count(groupTitles[lastIndex])) {
            for (size_t i = 0; i < groupTitles.size(); ++i) {
                if (!hiddenGroups.count(groupTitles[i])) {
                    lastIndex = (int)i;
                    break;
                }
            }
        }
        const std::string selectedGroup = groupTitles[lastIndex];
        selectedGroupIndex              = lastIndex;
        ProgramConfig::instance().setSettingItem(SettingItem::GROUP_SELECTED_INDEX, lastIndex);

        auto filtered = hiddenGroups.count(selectedGroup) ? tsvitch::LiveM3u8ListResult{}
                                                          : this->itemsForGroup(selectedGroup);
        brls::Logger::info("HomeLive: {} groups, selected '{}' with {} items", groupTitles.size(), selectedGroup,
                           filtered.size());

        // Position of the item to focus again (the series that was open before its episodes)
        size_t focusIndex = 0;
        bool restoreFocus = false;
        if (!restoreFocusId.empty()) {
            for (size_t i = 0; i < filtered.size(); i++) {
                if (filtered[i].id == restoreFocusId) {
                    focusIndex   = i;
                    restoreFocus = true;
                    break;
                }
            }
            restoreFocusId.clear();
        }

        if (this->isGenresGroup(selectedGroup))
            this->showGenreTiles();
        else if (filtered.empty())
            recyclingGrid->setEmpty();
        else
            recyclingGrid->setDataSource(
                new DataSourceLiveVideoList(std::move(filtered), this->isVirtualGroup(selectedGroup)));

        if (groupTitles.size() > 1) {
            upRecyclingGrid->setVisibility(brls::Visibility::VISIBLE);
            auto* upList = new DataSourceUpList(groupTitles, [this](const std::string& group) {
                this->selectGroupContent(group);
            });
            upList->markSelected(lastIndex);
            upRecyclingGrid->setDataSource(upList);
            upRecyclingGrid->selectRowAt(lastIndex, false);
        } else {
            upRecyclingGrid->setVisibility(brls::Visibility::GONE);
        }

        if (restoreFocus) recyclingGrid->selectRowAt(focusIndex, false);
        if (focusGridOnNextList) {
            focusGridOnNextList = false;
            this->focusContent();
        }

        // M3U8: salva in background con timestamp (non blocca UI)
        if (firstLoad && !isXtreamMode) {
            brls::Logger::info("HomeLive: First load detected, will save {} channels with timestamp (async)",
                               this->channelsList.size());
            auto toSave = this->channelsList;
            brls::Threading::async([data = std::move(toSave)]() {
                try {
                    ChannelManager::get()->saveWithTimestamp(data);
                    brls::Logger::info("HomeLive: Async saveWithTimestamp completed successfully");
                } catch (const std::exception& e) {
                    brls::Logger::error("HomeLive: Exception in async saveWithTimestamp: {}", e.what());
                } catch (...) {
                    brls::Logger::error("HomeLive: Unknown exception in async saveWithTimestamp");
                }
            });
        }
    });
}

void HomeLive::selectGroupIndex(size_t index) {
    auto* datasource = dynamic_cast<DataSourceUpList*>(upRecyclingGrid->getDataSource());
    if (!datasource) return;
    if (index >= datasource->getItemCount()) return;
    this->selectedGroupIndex = index;
    // setSelectedIndex dispara onGroupSelected -> selectGroupContent (que aplica o bloqueio)
    datasource->setSelectedIndex(upRecyclingGrid, index);
    upRecyclingGrid->selectRowAt(index, false);

    brls::Logger::debug("selectGroupIndex: {}", index);
}

void HomeLive::selectGroupContent(const std::string& group) {
    // Bloqueio parental: categoria travada e não liberada nesta sessão pede o PIN
    if (hiddenGroups.count(group)) {
        recyclingGrid->setEmpty();
        this->promptCategoryPin(group, [this, group]() {
            unlockedCategories.insert(group);
            hiddenGroups.erase(group);
            this->selectGroupContent(group);
        });
        return;
    }

    if (this->isGenresGroup(group)) {
        this->showGenreTiles();
        return;
    }
    waitingGenres = false;
    // The genre tiles changed the grid's columns
    this->applyGridLayout();
    auto filtered = this->itemsForGroup(group);
    if (filtered.empty())
        recyclingGrid->setEmpty();
    else
        recyclingGrid->setDataSource(new DataSourceLiveVideoList(std::move(filtered), this->isVirtualGroup(group)));
}

bool HomeLive::isVirtualGroup(const std::string& group) const {
    return isXtreamMode && !inSeriesEpisodes && (group == allGroupLabel() || group == recentGroupLabel());
}

bool HomeLive::isGenresGroup(const std::string& group) const {
    return isXtreamMode && !inSeriesEpisodes && currentLoadType != 0 && group == genresGroupLabel();
}

void HomeLive::showGenreTiles() {
    auto& catalog = Catalog::instance();
    if (!catalog.isLoaded()) {
        // The catalogue's changed event shows them
        waitingGenres = true;
        catalog.ensureLoaded();
        recyclingGrid->showSkeleton();
        return;
    }
    waitingGenres = false;
    // The movies' genres come from TMDB: the fetch may have waited for the catalogue
    tsvitch::TmdbService::instance().refresh();
    auto tiles                        = tsvitch::discover::genreTiles(currentLoadType);
    recyclingGrid->spanCount          = 4;
    recyclingGrid->estimatedRowHeight = 118;
    if (tiles.empty()) {
        bool tmdb = tsvitch::TmdbService::instance().enabled();
        recyclingGrid->setEmpty(currentLoadType == 1 && !tmdb ? "tsvitch/discover/genres_need_tmdb"_i18n
                                                              : "tsvitch/discover/genres_wait"_i18n);
        return;
    }
    recyclingGrid->setDataSource(new DataSourceGenreTiles(std::move(tiles)));
}

void HomeLive::promptCategoryPin(const std::string& category, std::function<void()> onUnlock) {
    brls::Application::getImeManager()->openForText(
        [this, category, onUnlock](const std::string& text) {
            if (text == ProgramConfig::instance().getParentalPin()) {
                onUnlock();
            } else {
                brls::Application::notify("tsvitch/parental/wrong_pin"_i18n);
            }
        },
        "tsvitch/parental/enter_pin"_i18n, "", 8, "", 0);
}

void HomeLive::toggleFavorite() {
    if (inHubMode) return;
    //get focus item
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item) return;

    //get channel
    tsvitch::LiveM3u8 channel = item->getChannel();

    FavoriteManager::get()->toggle(channel);

    if (FavoriteManager::get()->isFavorite(channel)) {
        item->setFavoriteIcon(true);
    } else {
        item->setFavoriteIcon(false);
    }
}

void HomeLive::search() {
    // Nothing to search in the hub (the last list stays in memory behind it)
    if (inHubMode || this->channelsList.empty()) return;
    // The dialog says what is searched: channels, movies or series
    const char* title = currentLoadType == 2   ? "tsvitch/xtream/search_label/series"
                        : currentLoadType == 1 ? "tsvitch/xtream/search_label/movies"
                                               : "tsvitch/xtream/search_label/live";
    brls::Application::getImeManager()->openForText([this](const std::string& text) { this->filter(text); },
                                                    brls::getStr(title), "", 32, "", 0);
}

void HomeLive::cancelSearch() {
    isSearchActive = false;
    if (currentGroups.empty()) return;
    int index = ProgramConfig::instance().getSettingItem(SettingItem::GROUP_SELECTED_INDEX, 0);
    if (index < 0 || index >= (int)currentGroups.size()) index = 0;
    upRecyclingGrid->setVisibility(currentGroups.size() > 1 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->selectGroupContent(currentGroups[index]);
    this->focusContent();
}

void HomeLive::filter(const std::string& key) {
    // Case and Turkish letters do not matter: "ates" finds "Ateş"
    std::string needle = tsvitch::foldForSearch(key);
    needle.erase(0, needle.find_first_not_of(' '));
    needle.erase(needle.find_last_not_of(' ') + 1);
    if (needle.empty()) return;

    isSearchActive = true;
    lastSearch     = key;
    tsvitch::LiveM3u8ListResult found;
    for (const auto& item : this->channelsList) {
        // Categories locked by the PIN stay hidden in the results too
        if (hiddenGroups.count(groupOf(item))) continue;
        if (tsvitch::foldForSearch(item.title).find(needle) != std::string::npos) found.push_back(item);
    }

    this->sortItems(found);
    upRecyclingGrid->setVisibility(brls::Visibility::GONE);
    // The results are posters even when the genre tiles were shown
    waitingGenres = false;
    this->applyGridLayout();
    if (found.empty()) {
        recyclingGrid->setEmpty();
        brls::Application::notify(brls::getStr("tsvitch/xtream/no_results", key));
    } else {
        brls::Application::notify(brls::getStr("tsvitch/xtream/search_results", found.size(), key));
        recyclingGrid->setDataSource(new DataSourceLiveVideoList(std::move(found)));
    }
    this->focusContent();
}

void HomeLive::onShow() {
    brls::Logger::info("Fragment HomeLive: onShow called");

    // If the 3-card hub is visible, load nothing until the user chooses
    if (inHubMode) {
        brls::Logger::debug("HomeLive onShow: hub visible, skipping load");
        return;
    }

    // Xtream lists are cached in memory and on the SD card: nothing to reload when coming back
    // (the old check looked at the M3U8 cache and downloaded the whole list again)
    if (isXtreamMode) return;

    // Se il caricamento iniziale è ancora in corso, non fare nulla
    if (isInitialLoadInProgress) {
        brls::Logger::debug("HomeLive onShow: Initial load still in progress, skipping");
        return;
    }
    
    // Smart refresh: controlla se abbiamo già canali in memoria
    if (!channelsList.empty()) {
        brls::Logger::debug("HomeLive onShow: Already have {} channels in memory, checking if refresh needed", channelsList.size());
        
        // Per decidere se ricaricare, controlla l'età della cache
        int iptvMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0);
        int maxCacheAge = (iptvMode == 1) ? 5 : 15; // Xtream: 5 min, M3U8: 15 min
        
        brls::Threading::async([this, maxCacheAge, iptvMode, validityFlag = this->validityFlag] {
            // Controlla se l'app è ancora valida prima di procedere
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive onShow: async task canceled - app exiting");
                return;
            }
            
            bool needsRefresh = !ChannelManager::get()->isCacheValid(maxCacheAge);
            
            brls::sync([this, needsRefresh, iptvMode, validityFlag]() {
                // Controlla di nuovo la validità prima di aggiornare l'UI
                if (!validityFlag || !validityFlag->load()) {
                    brls::Logger::debug("HomeLive onShow: sync task canceled - app exiting");
                    return;
                }
                
                if (needsRefresh) {
                    brls::Logger::info("HomeLive onShow: Cache expired, refreshing channels (IPTV mode: {})", iptvMode);
                    this->requestLiveList();
                } else {
                    brls::Logger::debug("HomeLive onShow: Cache still valid, no refresh needed");
                    // Solo ricarica i dati delle grid per aggiornare la UI
                    this->recyclingGrid->reloadData();
                    this->upRecyclingGrid->reloadData();
                }
            });
        });
        return;
    }
    
    // Se non abbiamo canali e il caricamento iniziale non è in corso, usa lo stesso meccanismo del costruttore
    brls::Logger::debug("HomeLive onShow: No channels in memory and no initial load in progress, loading...");
    
    int iptvMode = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0);
    brls::Threading::async([this, iptvMode, validityFlag = this->validityFlag] {
        // Controlla se l'app è ancora valida prima di procedere
        if (!validityFlag || !validityFlag->load()) {
            brls::Logger::debug("HomeLive onShow: fallback async task canceled - app exiting");
            return;
        }
        
        auto cachedChannels = ChannelManager::get()->loadIfValid();
        
        brls::sync([this, cachedChannels, validityFlag]() {
            // Controlla di nuovo la validità prima di aggiornare l'UI
            if (!validityFlag || !validityFlag->load()) {
                brls::Logger::debug("HomeLive onShow: fallback sync task canceled - app exiting");
                return;
            }
            
            if (!cachedChannels.empty()) {
                brls::Logger::info("HomeLive onShow: Using valid cached channels ({} channels)", cachedChannels.size());
                this->onLiveList(cachedChannels, false);
            } else {
                brls::Logger::info("HomeLive onShow: No valid cache, requesting fresh channels");
                this->requestLiveList();
            }
        });
    });
    
    brls::Logger::debug("HomeLive onShow: onShow completed");
}

void HomeLive::onCreate() {
    brls::Logger::debug("Fragment HomeLive: onCreate called");

    // Non fare niente qui - il caricamento è già gestito nel costruttore
    // in modo completamente asincrono per evitare blocchi dell'UI
    brls::Logger::debug("HomeLive onCreate: Delegating to constructor for async loading");
}

    // for (int i = 0; i < 100; ++i) {
    //     // Crea la sidebar item (puoi personalizzare label e stile)
    //    auto* item = new AutoSidebarItem();
    //         item->setTabStyle(AutoTabBarStyle::PLAIN);
    //         item->setLabel("Tab " + std::to_string(i + 1));
    //         item->setFontSize(18);

    //     // Funzione che crea la view associata al tab
    //        this->tabFrame->addTab(item, [this, i, item]() {
    //         // Qui puoi restituire una view diversa per ogni tab
    //         // Esempio: una semplice Box con un'etichetta
    //         auto* box = new brls::Box();
    //         auto* label = new brls::Label();
    //         label->setText("Contenuto Tab " + std::to_string(i + 1));
    //         box->addView(label);
    //         return box;
    //     });
    // }

HomeLive::~HomeLive() { 
    brls::Logger::debug("Fragment HomeLiveActivity: delete");
    if (loadObserverOwner == this) {
        CLIENT::setXtreamLoadObserver(nullptr);
        loadObserverOwner = nullptr;
    }
    
    // Cancella la sottoscrizione all'evento di uscita solo se è stata creata
    if (hasExitSubscription) {
        brls::Application::getExitEvent()->unsubscribe(exitEventSubscription);
    }
    
    // Invalidate the flag to prevent callbacks from accessing this object
    if (validityFlag) {
        validityFlag->store(false);
    }
    Catalog::instance().getChangedEvent()->unsubscribe(catalogSubscription);
}

brls::View* HomeLive::create() { 
    brls::Logger::debug("HomeLive::create() called - creating new HomeLive instance");
    return new HomeLive(); 
}

void HomeLive::downloadVideo() {
    if (inHubMode) return;
    // Ottieni l'item attualmente focalizzato
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item) {
        brls::Logger::warning("HomeLive::downloadVideo: No focused item");
        return;
    }
    tsvitch::startVideoDownload(item->getChannel());
}