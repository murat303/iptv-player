#pragma once

#include <functional>
#include <memory>

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>
#include <borealis/views/slider.hpp>

#include "utils/event_helper.hpp"
#include "api/tsvitch/result/xtream_detail.h"
#include "presenter/live_data.hpp"

class VideoView;
class NextEpisodeView;
namespace brls {
class Dialog;
}

class LiveActivity : public brls::Activity, public LiveDataRequest {
public:
    CONTENT_FROM_XML_RES("activity/video_activity.xml");

    // seriesPlaylist: the list holds the episodes of a series in order, so the next one can start by itself
    explicit LiveActivity(const std::vector<tsvitch::LiveM3u8>& channels, size_t startIndex,
                          std::function<void()> onClose = nullptr, bool seriesPlaylist = false);

    void setCommonData();

    void onContentAvailable() override;

    void onLiveData(std::string url) override;

    void onError(const std::string& error) override;

    std::vector<std::string> getQualityDescriptionList();
    int getCurrentQualityIndex();

    void retryRequestData();

    void startLive();

    void startAd(std::string adUrl);

    void detectContentType();

    // Plays another item of the list (next/previous buttons, next episode)
    void switchTo(size_t index);

    /// Index, in the list given to the player, of the item that played last (the list focuses it again)
    static size_t lastPlayedIndex() { return lastIndex; }



    ~LiveActivity() override;

protected:
    // Remembers where a movie or episode was left; one watched to the end starts from the beginning again
    void savePlaybackPosition();

    void onMpvEvent(MpvEventEnum event);

    // End of a video: an episode offers the next one
    void onVideoEnded();

    // Every second of a movie or episode: marks it watched at 90% and offers the next episode at the credits
    void onProgress();

    // Where the closing credits start: a chapter named so, otherwise the time chosen in the settings
    double findCreditsStart(double duration);

    // Time and length of the video that plays now, asked from mpv (false until both are known). The cached
    // values of MPVCore still belong to the previous video until the new one is loaded.
    bool currentProgress(double& position, double& duration);

    void offerNextEpisode();

    // Programme guide of a live Xtream channel under the title: asked once the channel stays a moment
    void loadEpg();

    // Shows the current and next programme and moves on when the current one ends
    void showEpg();

    void countDownNextEpisode(const std::shared_ptr<bool>& open, NextEpisodeView* view, brls::Dialog* dialog,
                              int seconds, size_t next);

    VideoView* video = nullptr;

    bool seriesPlaylist     = false;
    size_t nextEpisodeDelay = 0;
    // For the video that plays: the next episode was offered, where its credits start (-1: not known yet),
    // it was marked watched while playing
    bool nextOffered   = false;
    double creditsAt   = -1;
    bool markedWatched = false;
    // True while the next episode question is on the screen
    std::shared_ptr<bool> nextDialogOpen;

    std::vector<tsvitch::XtreamEpgEntry> epg;
    size_t epgDelay = 0;
    // False once the activity is gone: late answers and delays check it
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    std::function<void()> onCloseCallback;

    std::vector<tsvitch::LiveM3u8> channelList;
    size_t currentChannelIndex = 0;

    size_t toggleDelayIter = 0;

    size_t errorDelayIter = 0;

    bool isAd = false;

    tsvitch::LiveM3u8 liveData;

    MPVEvent::Subscription tl_event_id;
    bool mpvEventRegistered = false;

    CustomEvent::Subscription event_id;
    bool customEventRegistered = false;

private:
    static inline size_t lastIndex = 0;

   void getAdUrlFromServer(std::function<void(const std::string&)> callback = nullptr);
};