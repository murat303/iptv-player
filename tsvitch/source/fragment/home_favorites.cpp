//
// The favorites tab: live channels, movies and series, one kind at a time
//

#include <algorithm>
#include <cmath>
#include <utility>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>
#include <fmt/format.h>
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/custom_button.hpp"
#include "utils/image_helper.hpp"
#include "utils/activity_helper.hpp"
#include "activity/live_player_activity.hpp"
#include "fragment/home_favorites.hpp"
#include "core/FavoriteManager.hpp"
#include "core/HistoryManager.hpp"
#include "core/DownloadManager.hpp"
#include "utils/video_download.hpp"
#include "api/tsvitch/result/home_live_result.h"

using namespace brls::literals;

namespace {

// The kind shown last: the tab opens on it again while it has favorites
int lastKind = -1;

// The kind of a favorite: 0 live TV, 1 movie, 2 series (a series page, or an episode whose series is not known)
int kindOf(const tsvitch::LiveM3u8& item) {
    if (item.url.rfind("xtream-series://", 0) == 0) return 2;
    return item.type == 1 || item.type == 2 ? item.type : 0;
}

bool isPoster(int kind) { return kind == 1 || kind == 2; }

// Posters keep the 2:3 of the lists: the card's picture has a fixed height and fills the column's width, so here,
// beside the sidebar and wider than the lists, the height follows the width (seven columns as in the collections)
constexpr int POSTER_COLUMNS = 7;
constexpr float POSTER_TEXT  = 68;  // the title and the year under the picture

float posterHeight(RecyclingGrid* grid) {
    float width = grid->getWidth();
    if (width <= 0) width = 1180;  // the tab at 1280x720, before its first layout
    float column = (width - grid->getPaddingLeft() - grid->getPaddingRight()) / POSTER_COLUMNS - grid->estimatedRowSpace;
    return std::round(column * 1.5f);
}

}  // namespace

/// The chip row: the focus comes to the chip of the kind shown
class FavoriteKindChips : public brls::Box {
public:
    FavoriteKindChips() : brls::Box(brls::Axis::ROW) { this->setAlignItems(brls::AlignItems::CENTER); }

    brls::View* getDefaultFocus() override { return selected ? selected : brls::Box::getDefaultFocus(); }

    brls::View* selected = nullptr;
};

// The favorites of one kind
class DataSourceFavoriteChannels : public RecyclingGridDataSource {
public:
    explicit DataSourceFavoriteChannels(std::vector<tsvitch::LiveM3u8> favorites)
        : favoriteChannels(std::move(favorites)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        const auto& r = favoriteChannels[index];
        // Movies and series use the poster card of the lists
        bool poster = isPoster(kindOf(r));
        auto* item  = (RecyclingGridItemLiveVideoCard*)recycler->dequeueReusableCell(poster ? "Poster" : "Cell");
        if (poster) item->setPosterHeight(posterHeight(recycler));
        item->setChannel(r);
        return item;
    }

    size_t getItemCount() override { return favoriteChannels.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        const auto& item = favoriteChannels[index];
        // Movies and series open their information screen (a series item is not a playable url)
        if (item.type == 1 || item.url.rfind("xtream-series://", 0) == 0) {
            Intent::openXtreamDetail(item, [recycler]() { RecyclingGridItemLiveVideoCard::refreshWatchStates(recycler); });
            return;
        }
        HistoryManager::get()->add(item);
        size_t start  = 0;
        auto playlist = sameKindPlaylist(favoriteChannels, index, start);
        int kind      = kindOf(item);
        Intent::openLive(playlist, start, [recycler, playlist, kind]() {
            // The favorite that played last (next/previous buttons) gets the focus
            std::vector<tsvitch::LiveM3u8> favorites;
            for (const auto& favorite : FavoriteManager::get()->getFavorites())
                if (kindOf(favorite) == kind) favorites.push_back(favorite);
            size_t last = LiveActivity::lastPlayedIndex(), focus = 0;
            for (size_t i = 0; last < playlist.size() && i < favorites.size(); i++)
                if (favorites[i].url == playlist[last].url) focus = i;
            recycler->reloadWithFocus(focus, new DataSourceFavoriteChannels(favorites));
        });
    }

    void clearData() override { favoriteChannels.clear(); }

private:
    std::vector<tsvitch::LiveM3u8> favoriteChannels;
};

/// HomeFavorites

HomeFavorites::HomeFavorites() {
    this->inflateFromXMLRes("xml/fragment/home_favorites.xml");
    recyclingGrid->registerCell("Cell", []() { return RecyclingGridItemLiveVideoCard::create(); });
    recyclingGrid->registerCell("Poster", []() { return RecyclingGridItemLiveVideoCard::createPoster(); });
    kindPrev->setText("");
    kindNext->setText("");
    this->reload(0);

    this->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](...) {
        this->toggleFavorite();
        return true;
    });

    this->registerAction("tsvitch/download/action"_i18n, brls::BUTTON_RT, [this](...) {
        this->downloadVideo();
        return true;
    });

    // L and R change the kind; the glyphs beside the chips are their hints
    this->registerAction(
        "", brls::BUTTON_LB,
        [this](brls::View*) {
            this->stepKind(-1);
            return true;
        },
        true);
    this->registerAction(
        "", brls::BUTTON_RB,
        [this](brls::View*) {
            this->stepKind(1);
            return true;
        },
        true);
}

void HomeFavorites::onCreate() { this->refreshFavorites(); }

void HomeFavorites::onShow() { this->refreshFavorites(); }

void HomeFavorites::refreshFavorites() { this->reload(0); }

brls::View* HomeFavorites::getDefaultFocus() {
    if (!this->favoritesOf(lastKind).empty())
        if (brls::View* view = recyclingGrid->getDefaultFocus()) return view;
    return AttachedView::getDefaultFocus();
}

std::vector<tsvitch::LiveM3u8> HomeFavorites::favoritesOf(int kind) const {
    std::vector<tsvitch::LiveM3u8> items;
    for (const auto& favorite : favoritesList)
        if (kindOf(favorite) == kind) items.push_back(favorite);
    return items;
}

void HomeFavorites::reload(size_t focus) {
    // Episodes saved as favorites by an earlier version show as their series
    FavoriteManager::get()->convertEpisodes();
    this->favoritesList = FavoriteManager::get()->getFavorites();
    kinds.clear();
    for (int kind : {0, 1, 2})
        if (!this->favoritesOf(kind).empty()) kinds.push_back(kind);
    if (std::find(kinds.begin(), kinds.end(), lastKind) == kinds.end()) {
        lastKind = kinds.empty() ? -1 : kinds.front();
        focus    = 0;
    }
    this->buildKindChips();
    this->showKind(lastKind, focus);
}

bool HomeFavorites::focusInGrid() {
    for (brls::View* view = brls::Application::getCurrentFocus(); view; view = view->getParent())
        if (view == recyclingGrid) return true;
    return false;
}

void HomeFavorites::buildKindChips() {
    // Chips only when there is a choice: one kind needs none
    std::vector<int> wanted = kinds.size() > 1 ? kinds : std::vector<int>{};
    std::vector<int> built;
    for (const auto& chip : kindChips) built.push_back(chip.kind);
    if (built != wanted) {
        // A focused chip must not be deleted under the focus
        bool chipFocused = false;
        for (brls::View* view = brls::Application::getCurrentFocus(); view && !chipFocused; view = view->getParent())
            chipFocused = view == kindBox;
        if (chipFocused) brls::Application::giveFocus(this->getTabBar());
        kindBox->clearViews();
        kindChips.clear();
        chipRow = nullptr;
        if (!wanted.empty()) {
            chipRow = new FavoriteKindChips();
            for (int kind : wanted) {
                auto* chip = new CustomButton();
                chip->setFocusable(true);
                chip->setHeight(38);
                chip->setPaddingLeft(16);
                chip->setPaddingRight(16);
                chip->setMarginRight(10);
                chip->setCornerRadius(8);
                chip->setHighlightCornerRadius(8);
                chip->setHideHighlightBackground(true);
                chip->setAlignItems(brls::AlignItems::CENTER);
                auto* label = new brls::Label();
                label->setFontSize(17);
                label->setSingleLine(true);
                label->setTextColor(nvgRGB(255, 255, 255));
                chip->addView(label);
                chip->registerClickAction([this, kind](brls::View*) {
                    this->showKind(kind);
                    return true;
                });
                chip->addGestureRecognizer(new brls::TapGestureRecognizer(chip));
                chipRow->addView(chip);
                kindChips.push_back({chip, label, kind});
            }
            kindBox->addView(chipRow);
        }
    }
    bool several = !kindChips.empty();
    kindPrev->setVisibility(several ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    kindNext->setVisibility(several ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    this->updateKindChips();
}

void HomeFavorites::updateKindChips() {
    static const NVGcolor selected   = nvgRGB(255, 145, 0);
    static const NVGcolor other      = nvgRGBA(255, 255, 255, 38);
    static const char* const names[] = {"tsvitch/xtream/content/live", "tsvitch/xtream/content/movies",
                                        "tsvitch/xtream/content/series"};
    for (const auto& chip : kindChips) {
        chip.label->setText(fmt::format("{} ({})", brls::getStr(names[chip.kind]), this->favoritesOf(chip.kind).size()));
        chip.button->setBackgroundColor(chip.kind == lastKind ? selected : other);
        if (chip.kind == lastKind && chipRow) chipRow->selected = chip.button;
    }
}

void HomeFavorites::showKind(int kind, size_t focus) {
    lastKind    = kind;
    bool poster = isPoster(kind);
    recyclingGrid->spanCount          = poster ? POSTER_COLUMNS : 4;
    recyclingGrid->estimatedRowHeight = poster ? posterHeight(recyclingGrid) + POSTER_TEXT : 200;
    // The focus stays in the grid when it was there
    recyclingGrid->reloadWithFocus(focus, new DataSourceFavoriteChannels(this->favoritesOf(kind)));
    this->updateKindChips();
}

void HomeFavorites::stepKind(int step) {
    auto it = std::find(kinds.begin(), kinds.end(), lastKind);
    if (kinds.size() < 2 || it == kinds.end()) return;
    brls::View* focus = brls::Application::getCurrentFocus();
    long next         = (it - kinds.begin()) + step;
    if (next < 0 || next >= (long)kinds.size()) {
        if (focus) focus->shakeHighlight(step < 0 ? brls::FocusDirection::LEFT : brls::FocusDirection::RIGHT);
        return;
    }
    bool onChip = false;
    for (const auto& chip : kindChips) onChip = onChip || chip.button == focus;
    this->showKind(kinds[next]);
    // On a chip the focus moves along to the chip of the new kind
    if (onChip)
        for (const auto& chip : kindChips)
            if (chip.kind == lastKind) brls::Application::giveFocus(chip.button);
}

void HomeFavorites::toggleFavorite() {
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item || !this->focusInGrid()) return;

    tsvitch::LiveM3u8 channel = item->getChannel();
    size_t index              = item->getIndex();
    brls::Logger::debug("toggleFavorite: {}", channel.title);
    FavoriteManager::get()->toggle(channel);

    // It left the tab: the focus goes to the next card, to the first card of another kind when this one has none
    // left, and back to the sidebar without favorites
    this->reload(index);
    if (kinds.empty()) brls::Application::giveFocus(this->getTabBar());
}

void HomeFavorites::downloadVideo() {
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item || !this->focusInGrid()) {
        brls::Logger::warning("HomeFavorites::downloadVideo: No focused item");
        return;
    }
    tsvitch::LiveM3u8 channel = item->getChannel();
    tsvitch::startVideoDownload(channel);
}

brls::View* HomeFavorites::create() { return new HomeFavorites(); }

HomeFavorites::~HomeFavorites() = default;

void HomeFavorites::onError(const std::string& error) {
    brls::sync([this, error]() { this->recyclingGrid->setError(error); });
}
