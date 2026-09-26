//
// Mostra i canali recenti visti dall'utente
//

#include <utility>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/dialog.hpp>
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/custom_button.hpp"
#include "utils/image_helper.hpp"
#include "utils/activity_helper.hpp"
#include "fragment/home_history.hpp"
#include "core/HistoryManager.hpp"
#include "core/FavoriteManager.hpp"
#include "core/DownloadManager.hpp"
#include "utils/video_download.hpp"
#include "utils/stream_helper.hpp"

using namespace brls::literals;

// DataSource per i canali recenti
class DataSourceRecentChannels : public RecyclingGridDataSource {
public:
    explicit DataSourceRecentChannels(const std::deque<tsvitch::LiveM3u8>& recent)
        : recentChannels(recent.begin(), recent.end()) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        const auto& r                        = recentChannels[index];
        RecyclingGridItemLiveVideoCard* item = (RecyclingGridItemLiveVideoCard*)recycler->dequeueReusableCell("Cell");
        item->setChannel(r);
        return item;
    }

    size_t getItemCount() override { return recentChannels.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        // A series item is not a playable url: open its information screen
        if (recentChannels[index].url.rfind("xtream-series://", 0) == 0) {
            Intent::openXtreamDetail(recentChannels[index]);
            return;
        }
        HistoryManager::get()->add(recentChannels[index]);
        size_t start = 0;
        Intent::openLive(sameKindPlaylist(recentChannels, index, start), start, [recycler]() {
            // The item just watched is the newest one
            auto recent = HistoryManager::get()->recent(40);
            recycler->reloadWithFocus(0, new DataSourceRecentChannels(recent));
        });
    }

    void clearData() override { recentChannels.clear(); }

private:
    std::vector<tsvitch::LiveM3u8> recentChannels;
};

/// HomeHistory

HomeHistory::HomeHistory() {
    this->inflateFromXMLRes("xml/fragment/home_history.xml");
    recyclingGrid->registerCell("Cell", []() { return RecyclingGridItemLiveVideoCard::create(); });

    this->showRecent(0);

    this->registerAction("tsvitch/history/remove"_i18n, brls::BUTTON_Y, [this](...) {
        this->removeFocused();
        return true;
    });
    clearButton->registerClickAction([this](brls::View*) {
        this->confirmClear();
        return true;
    });
    clearButton->addGestureRecognizer(new brls::TapGestureRecognizer(clearButton));

    this->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](...) {
        this->toggleFavorite();
        return true;
    });
    
    this->registerAction("tsvitch/download/action"_i18n, brls::BUTTON_RT, [this](...) {
        this->downloadVideo();
        return true;
    });
    
}

void HomeHistory::toggleFavorite(){
    //get focus item
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item) return;

    //get channel
    tsvitch::LiveM3u8 channel = item->getChannel();

   
    FavoriteManager::get()->toggle(channel);

    if (FavoriteManager::get()->isFavorite(channel.url)) {
        item->setFavoriteIcon(true);
    } else {
        item->setFavoriteIcon(false);
    }
}

void HomeHistory::onCreate() { this->refreshRecent(); }

void HomeHistory::onShow() { this->refreshRecent(); }

brls::View* HomeHistory::getDefaultFocus() {
    brls::View* first = recyclingGrid->getDefaultFocus();
    return first ? first : AttachedView::getDefaultFocus();
}

void HomeHistory::refreshRecent() { this->showRecent(0); }

void HomeHistory::showRecent(size_t focus) {
    auto recent = HistoryManager::get()->recent(40);
    if (recent.empty()) {
        recyclingGrid->setEmpty("tsvitch/history/empty"_i18n);
        return;
    }
    recyclingGrid->reloadWithFocus(focus, new DataSourceRecentChannels(recent));
}

void HomeHistory::removeFocused() {
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item) return;
    size_t index = item->getIndex();
    HistoryManager::get()->remove(item->getChannel().url);
    brls::Application::notify("tsvitch/history/removed"_i18n);
    if (HistoryManager::get()->recent(1).empty()) {
        // The last one is gone: the focus moves to the button above the empty list
        brls::Application::giveFocus(clearButton);
        this->showRecent(0);
        return;
    }
    this->showRecent(index);
}

void HomeHistory::confirmClear() {
    if (HistoryManager::get()->recent(1).empty()) return;
    // "Cancel" comes first: it has the focus, so a quick A press clears nothing
    auto* dialog = new brls::Dialog("tsvitch/history/clear_confirm"_i18n);
    dialog->addButton("tsvitch/download/close"_i18n, []() {});
    dialog->addButton("tsvitch/history/clear_all"_i18n, [this]() {
        HistoryManager::get()->clearAll();
        this->showRecent(0);
        brls::Application::notify("tsvitch/parental/history_cleared"_i18n);
    });
    dialog->open();
}

brls::View* HomeHistory::create() { return new HomeHistory(); }

HomeHistory::~HomeHistory() = default;

void HomeHistory::onError(const std::string& error) {
    brls::sync([this, error]() { this->recyclingGrid->setError(error); });
}

void HomeHistory::downloadVideo() {
    // Ottieni l'item attualmente focalizzato
    auto* item = dynamic_cast<RecyclingGridItemLiveVideoCard*>(this->recyclingGrid->getFocusedItem());
    if (!item) {
        brls::Logger::warning("HomeHistory::downloadVideo: No focused item");
        return;
    }

    // Ottieni il canale
    tsvitch::LiveM3u8 channel = item->getChannel();
    tsvitch::startVideoDownload(channel);
}
