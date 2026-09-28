

#include "activity/discover_list_activity.hpp"
#include <iostream>
#include <borealis/core/application.hpp>

#include "activity/live_player_activity.hpp"

#include "activity/settings_activity.hpp"
#include "activity/trailer_activity.hpp"

#include "activity/main_activity.hpp"
#include "activity/hint_activity.hpp"
#include "activity/xtream_detail_activity.hpp"

#include "utils/activity_helper.hpp"
#include "utils/config_helper.hpp"
#include "core/DownloadManager.hpp"

#include <borealis/core/i18n.hpp>
#include <borealis/views/dialog.hpp>

using namespace brls::literals;

namespace {
bool closing = false;

// Once, before the first screen with a close callback opens
void watchClosing() {
    static bool subscribed = false;
    if (subscribed) return;
    subscribed = true;
    brls::Application::getExitEvent()->subscribe([]() { closing = true; });
}
}  // namespace

bool Intent::isClosing() { return closing; }

std::vector<tsvitch::LiveM3u8> sameKindPlaylist(const std::vector<tsvitch::LiveM3u8>& items, size_t index,
                                                size_t& start) {
    std::vector<tsvitch::LiveM3u8> playlist;
    start = 0;
    if (index >= items.size()) return playlist;
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].type != items[index].type || items[i].url.rfind("xtream-series://", 0) == 0) continue;
        if (i == index) start = playlist.size();
        playlist.push_back(items[i]);
    }
    return playlist;
}

void Intent::openLive(const std::vector<tsvitch::LiveM3u8>& channelList, size_t index, std::function<void()> onClose,
                      bool seriesPlaylist) {
    // The IPTV account allows one connection: watching over the network waits for the running download
    bool localFile = index < channelList.size() && channelList[index].url.rfind("file://", 0) == 0;
    DownloadItem active;
    if (!localFile && DownloadManager::instance().getActiveDownload(active)) {
        auto* dialog = new brls::Dialog(brls::getStr("tsvitch/download/busy_watch", active.title));
        dialog->addButton("tsvitch/download/pause_and_watch"_i18n,
                          [channelList, index, onClose, seriesPlaylist, id = active.id]() {
                              DownloadManager::instance().pauseDownload(id);
                              Intent::openLive(channelList, index, onClose, seriesPlaylist);
                          });
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
        return;
    }

    watchClosing();
    auto activity = new LiveActivity(channelList, index, onClose, seriesPlaylist);
    brls::Application::pushActivity(activity, brls::TransitionAnimation::NONE);
    registerFullscreen(activity);
}

void Intent::openTrailer(const std::string& title, const std::vector<tsvitch::TmdbVideo>& videos) {
    watchClosing();
    auto activity = new TrailerActivity(title, videos);
    brls::Application::pushActivity(activity, brls::TransitionAnimation::NONE);
    registerFullscreen(activity);
}

void Intent::openXtreamDetail(const tsvitch::LiveM3u8& item, std::function<void()> onClose) {
    watchClosing();
    auto activity = new XtreamDetailActivity(item, std::move(onClose));
    brls::Application::pushActivity(activity);
    registerFullscreen(activity);
}

void Intent::openDiscoverList(const std::string& collectionId, const std::string& title,
                              std::function<void()> onClose) {
    watchClosing();
    auto activity = new DiscoverListActivity(collectionId, title, std::move(onClose));
    brls::Application::pushActivity(activity);
    registerFullscreen(activity);
}

void Intent::openSettings(std::function<void()> onClose) {
    auto activity = new SettingsActivity(onClose);
    brls::Application::pushActivity(activity);
    registerFullscreen(activity);
}

void Intent::openHint() { brls::Application::pushActivity(new HintActivity()); }

void Intent::openMain() {
    auto activity = new MainActivity();
    brls::Application::pushActivity(activity);
    registerFullscreen(activity);
}

void Intent::_registerFullscreen(brls::Activity* activity) { (void)activity; }
