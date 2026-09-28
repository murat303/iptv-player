#include "activity/discover_list_activity.hpp"

#include <algorithm>

#include <borealis/core/i18n.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/label.hpp>

#include "core/FavoriteManager.hpp"
#include "utils/activity_helper.hpp"
#include "utils/discover.hpp"
#include "utils/text_fold.hpp"
#include "view/custom_button.hpp"
#include "view/grid_dropdown.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"

using namespace brls::literals;

namespace {

class DiscoverListSource : public RecyclingGridDataSource {
public:
    explicit DiscoverListSource(std::vector<tsvitch::LiveM3u8> items) : items(std::move(items)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* card  = (RecyclingGridItemLiveVideoCard*)recycler->dequeueReusableCell("Poster");
        auto shown  = items[index];
        shown.title = tsvitch::discover::cleanTitle(shown.title);
        card->setChannel(shown, false);
        return card;
    }

    const tsvitch::LiveM3u8* itemAt(size_t index) const { return index < items.size() ? &items[index] : nullptr; }

    size_t getItemCount() override { return items.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        Intent::openXtreamDetail(items[index],
                                 [recycler]() { RecyclingGridItemLiveVideoCard::refreshWatchStates(recycler); });
    }

    void clearData() override { items.clear(); }

private:
    std::vector<tsvitch::LiveM3u8> items;
};

const char* SORT_KEYS[] = {"tsvitch/discover/sort/suggested", "tsvitch/xtream/sort/rating", "tsvitch/xtream/sort/year",
                           "tsvitch/xtream/sort/recent", "tsvitch/xtream/sort/name"};

}  // namespace

DiscoverListActivity::DiscoverListActivity(std::string collectionId, std::string title, std::function<void()> onClose)
    : collectionId(std::move(collectionId)), title(std::move(title)), onClose(std::move(onClose)) {}

DiscoverListActivity::~DiscoverListActivity() {
    // Not while the app closes: the screens below are deleted first then
    if (onClose && !Intent::isClosing()) onClose();
}

void DiscoverListActivity::onContentAvailable() {
    grid->registerCell("Poster", []() { return RecyclingGridItemLiveVideoCard::createPoster(); });
    // The first poster gets the focus, not the sort button above it
    root->setDefaultFocusedIndex(1);

    auto collection = tsvitch::discover::collection(collectionId);
    items           = std::move(collection.items);
    titleLabel->setText(collection.title.empty() ? title : collection.title);
    countLabel->setText(tsvitch::discover::countText(items.size()));

    this->updateSortLabel();
    sortButton->registerClickAction([this](brls::View*) {
        this->pickSort();
        return true;
    });
    sortButton->addGestureRecognizer(new brls::TapGestureRecognizer(sortButton));
    this->registerAction("tsvitch/xtream/sort/title"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        this->pickSort();
        return true;
    });
    this->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](brls::View*) {
        auto* card   = dynamic_cast<RecyclingGridItemLiveVideoCard*>(grid->getFocusedItem());
        auto* source = dynamic_cast<DiscoverListSource*>(grid->getDataSource());
        if (!card || !source) return true;
        // The item as the provider named it (the card shows a shorter name)
        const tsvitch::LiveM3u8* item = source->itemAt(card->getIndex());
        if (!item) return true;
        FavoriteManager::get()->toggle(*item);
        card->setFavoriteIcon(FavoriteManager::get()->isFavorite(*item));
        return true;
    });
    this->show();
}

void DiscoverListActivity::show(size_t focus) {
    std::vector<tsvitch::LiveM3u8> sorted = items;
    auto by = [&sorted](auto better) { std::stable_sort(sorted.begin(), sorted.end(), better); };
    switch (sortMode) {
        case 1:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.rating > b.rating; });
            break;
        case 2:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.year > b.year; });
            break;
        case 3:
            by([](const tsvitch::LiveM3u8& a, const tsvitch::LiveM3u8& b) { return a.added > b.added; });
            break;
        case 4: {
            std::vector<std::pair<std::string, size_t>> keys;
            for (size_t i = 0; i < sorted.size(); i++) keys.emplace_back(tsvitch::foldForSearch(sorted[i].title), i);
            std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<tsvitch::LiveM3u8> byName;
            for (const auto& key : keys) byName.push_back(sorted[key.second]);
            sorted.swap(byName);
            break;
        }
        default:
            break;
    }
    if (sorted.empty()) {
        grid->setEmpty("tsvitch/discover/list_empty"_i18n);
        return;
    }
    grid->setDataSource(new DiscoverListSource(std::move(sorted)));
    if (focus > 0) grid->selectRowAt(focus, false);
}

void DiscoverListActivity::updateSortLabel() {
    int mode = sortMode >= 0 && sortMode < 5 ? sortMode : 0;
    sortLabel->setText(brls::getStr("tsvitch/xtream/sort/label", brls::getStr(SORT_KEYS[mode])));
}

void DiscoverListActivity::pickSort() {
    std::vector<std::string> names;
    for (const char* key : SORT_KEYS) names.push_back(brls::getStr(key));
    BaseDropdown::text(
        "tsvitch/xtream/sort/title"_i18n, names,
        [this](int mode) {
            if (mode < 0 || mode == sortMode) return;
            sortMode = mode;
            this->updateSortLabel();
            this->show();
            brls::Application::giveFocus(grid);
        },
        sortMode);
}
