#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>

#include "activity/live_player_activity.hpp"
#include "utils/number_helper.hpp"

#include <vector>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <fmt/format.h>

#include "tsvitch.h"

#include "utils/shader_helper.hpp"
#include "utils/config_helper.hpp"
#include "utils/stream_helper.hpp"
#include "utils/playback_position_manager.hpp"
#include "utils/watched_manager.hpp"
#include "utils/activity_helper.hpp"

#include "view/video_view.hpp"

#include "view/grid_dropdown.hpp"
#include "view/qr_image.hpp"
#include "view/mpv_core.hpp"

#include "config/server_config.h"

#include "core/FavoriteManager.hpp"
#include "core/HistoryManager.hpp"

using namespace brls::literals;

/// Content of the "next episode" dialog: tells the countdown when the dialog is gone
class NextEpisodeView : public brls::Box {
public:
    NextEpisodeView(std::shared_ptr<bool> open, const std::string& title) : open(std::move(open)) {
        this->setAxis(brls::Axis::COLUMN);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(40, 40, 30, 40);
        auto* heading = new brls::Label();
        heading->setFontSize(20);
        heading->setText("tsvitch/player/next_episode/title"_i18n);
        auto* name = new brls::Label();
        name->setFontSize(24);
        name->setMarginTop(12);
        name->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        name->setText(title);
        countdown = new brls::Label();
        countdown->setFontSize(18);
        countdown->setMarginTop(16);
        this->addView(heading);
        this->addView(name);
        this->addView(countdown);
    }

    ~NextEpisodeView() override { *open = false; }

    void setSeconds(int seconds) { countdown->setText(brls::getStr("tsvitch/player/next_episode/countdown", seconds)); }

private:
    std::shared_ptr<bool> open;
    brls::Label* countdown = nullptr;
};

LiveActivity::LiveActivity(const std::vector<tsvitch::LiveM3u8>& channels, size_t startIndex,
                           std::function<void()> onClose, bool seriesPlaylist)
    : onCloseCallback(onClose), channelList(channels), currentChannelIndex(startIndex), seriesPlaylist(seriesPlaylist) {
    this->liveData = channelList[currentChannelIndex];
    brls::Logger::debug("LiveActivity: create: {}", liveData.title);
    ShaderHelper::instance().clearShader(false);
}

void LiveActivity::onContentAvailable() {
    brls::Logger::debug("LiveActivity: onContentAvailable");

    // Ottieni i riferimenti agli elementi UI
    video = dynamic_cast<VideoView*>(this->getView("video"));

    // One subscription for the whole activity: the handler follows the item that is playing
    this->tl_event_id  = MPVCore::instance().getEvent()->subscribe([this](MpvEventEnum event) { this->onMpvEvent(event); });
    mpvEventRegistered = true;

    MPVCore::instance().setAspect(
        ProgramConfig::instance().getSettingItem(SettingItem::PLAYER_ASPECT, std::string{"-1"}));

    this->video->registerAction("", brls::BUTTON_B, [this](...) {
        if (this->video->isOSDLock()) {
            this->video->toggleOSD();
        } else {
            if (this->video->getTvControlMode() && this->video->isOSDShown()) {
                this->video->toggleOSD();
                return true;
            }
            brls::Logger::debug("exit live");
            brls::Application::popActivity();
        }
        return true;
    });

    this->video->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](...) {
        this->video->toggleFavorite();
        return true;
    });

    // L and R change the channel on live TV only: on a movie R closed the player, and between episodes an
    // accidental press changed the episode. At the ends of the channel list they do nothing.
    if (liveData.type == 0) {
        this->video->registerAction("hints/next_channel"_i18n, brls::BUTTON_RB, [this](...) {
            if (this->isAd) return true;
            if (this->video->isOSDLock())
                this->video->toggleOSD();
            else if (currentChannelIndex + 1 < channelList.size())
                this->switchTo(currentChannelIndex + 1);
            return true;
        });
        this->video->registerAction("hints/previous_channel"_i18n, brls::BUTTON_LB, [this](...) {
            if (this->isAd) return true;
            if (this->video->isOSDLock())
                this->video->toggleOSD();
            else if (currentChannelIndex > 0)
                this->switchTo(currentChannelIndex - 1);
            return true;
        });
    }

    this->video->hideSubtitleSetting();
    this->video->hideVideoRelatedSetting();
    this->video->hideBottomLineSetting();
    this->video->hideHighlightLineSetting();
    this->video->disableCloseOnEndOfFile();
    this->video->setFullscreenIcon(true);
    this->video->setTitle(liveData.title);
    this->video->setFavoriteIcon(FavoriteManager::get()->isFavorite(liveData.url));
    this->video->setStatusLabelLeft("");
    this->video->setFavoriteCallback([this](bool state) { FavoriteManager::get()->toggle(this->liveData); });

    this->getAdUrlFromServer([&](const std::string& adUrl) {
        brls::Logger::debug("LiveActivity: adUrl: {}", adUrl);
        if (!adUrl.empty()) {
            this->startAd(adUrl);
        } else {
            this->startLive();
        }
    });

    GA("open_live", {
        {"title", this->liveData.title},
        {"url", this->liveData.url},
        {"group", this->liveData.groupTitle},
        {"index", std::to_string(currentChannelIndex)},
    });
}
void LiveActivity::startAd(std::string adUrl) {
    brls::Logger::debug("LiveActivity: adUrl: {}", adUrl);
    this->isAd = true;
    this->video->setAdMode();
    this->video->showVideoProgressSlider();
    this->video->disableProgressSliderSeek(true); // Disabilita il seek durante gli annunci
    this->video->setUrl(adUrl);

    // Quando l'annuncio finisce normalmente, passa alla live
    this->video->setOnEndCallback([this]() { this->startLive(); });
}

void LiveActivity::startLive() {
    this->isAd = false;
    
    // Rileva il tipo di contenuto PRIMA di caricare l'URL
    // Questo imposta correttamente isLiveMode per eventuali errori di caricamento
    this->detectContentType();
    
    // Riabilita il seek quando non è più un annuncio
    this->video->disableProgressSliderSeek(false);
    
    this->video->setCustomToggleAction([this]() {
        if (MPVCore::instance().isStopped()) {
            this->onLiveData(this->liveData.url);
        } else if (MPVCore::instance().isPaused()) {
            MPVCore::instance().resume();
        } else {
            this->video->showOSD(false);
            MPVCore::instance().pause();
            // A paused live stream is closed after a while (it would fall behind); a movie stays paused
            if (!tsvitch::isLiveStream(liveData.url, liveData.title)) return;
            brls::cancelDelay(toggleDelayIter);
            ASYNC_RETAIN
            toggleDelayIter = brls::delay(5000, [ASYNC_TOKEN]() {
                ASYNC_RELEASE
                if (MPVCore::instance().isPaused()) {
                    MPVCore::instance().stop();
                }
            });
        }
    });
    
    this->video->setOnEndCallback([this]() { this->onVideoEnded(); });
    // A track's place in the list is remembered within the same series (or the same video)
    MPVCore::instance().trackContext = seriesPlaylist && !channelList.empty() ? channelList.front().url : liveData.url;
    this->video->setUrl(liveData.url);
    this->loadEpg();
}

/// "20:45" in the console's time zone
static std::string clockText(int64_t unixTime) {
    std::time_t time = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[8];
    std::strftime(text, sizeof(text), "%H:%M", &local);
    return text;
}

void LiveActivity::loadEpg() {
    brls::cancelDelay(epgDelay);
    epg.clear();
    this->video->setEpg("", "", -1, "", "");
    bool xtream = ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 1;
    if (!xtream || liveData.type != 0 || liveData.id.empty()) return;
    // Quick zapping sends nothing to the server: the guide is asked once the channel stays 1.5 s
    std::string id = liveData.id;
    auto alive     = this->alive;
    epgDelay       = brls::delay(1500, [this, alive, id]() {
        if (!*alive || liveData.id != id) return;
        CLIENT::get_xtream_short_epg(id, 4, [this, alive, id](std::vector<tsvitch::XtreamEpgEntry> entries) {
            if (!*alive || liveData.id != id) return;
            epg = std::move(entries);
            this->showEpg();
        });
    });
}

void LiveActivity::showEpg() {
    brls::cancelDelay(epgDelay);
    auto now = static_cast<int64_t>(std::time(nullptr));
    while (!epg.empty() && epg.front().end <= now) epg.erase(epg.begin());
    if (epg.empty()) {
        this->video->setEpg("", "", -1, "", "");
        return;
    }
    const auto& first = epg.front();
    bool onAir        = first.start <= now;
    const auto* next  = onAir ? (epg.size() > 1 ? &epg[1] : nullptr) : &first;
    if (onAir) {
        float progress = static_cast<float>(now - first.start) / static_cast<float>(first.end - first.start);
        this->video->setEpg(clockText(first.start) + " – " + clockText(first.end), first.title, progress,
                            next ? clockText(next->start) : "", next ? next->title : "");
    } else {
        this->video->setEpg("", "", -1, clockText(first.start), first.title);
    }

    // The progress bar moves every half minute; when the programme ends the next one moves up and the
    // list is asked again when it runs out
    int64_t untilChange = (onAir ? first.end : first.start) - now;
    int64_t wait        = std::clamp<int64_t>(untilChange, 1, 30);
    auto alive          = this->alive;
    epgDelay            = brls::delay(wait * 1000 + 500, [this, alive, untilChange, wait]() {
        if (!*alive) return;
        if (untilChange <= wait && epg.size() <= 1)
            this->loadEpg();
        else
            this->showEpg();
    });
}

void LiveActivity::onMpvEvent(MpvEventEnum event) {
    if (event == MpvEventEnum::MPV_FILE_ERROR && this->isAd) {
        brls::Logger::warning("LiveActivity: Ad failed to load, skipping to live content");
        this->startLive();
        return;
    }
    if (event == MpvEventEnum::UPDATE_PROGRESS) {
        this->onProgress();
        return;
    }
    if (event != MpvEventEnum::MPV_LOADED || this->isAd) return;
    // The audio and subtitle tracks picked in an earlier episode, also when they have no language
    MPVCore::instance().applyTrackChoices();
    // The real duration tells a video from a live stream
    this->detectContentType();
    // A movie or episode continues where it was left
    if (!tsvitch::isLiveStream(liveData.url, liveData.title)) {
        int64_t savedPosition = tsvitch::PlaybackPositionManager::getPosition(liveData.url);
        if (savedPosition > 0) {
            MPVCore::instance().seek(savedPosition);
            brls::Logger::info("LiveActivity: Restored playback position to {} seconds", savedPosition);
        }
    }
}

void LiveActivity::switchTo(size_t index) {
    if (index >= channelList.size()) return;
    brls::cancelDelay(nextEpisodeDelay);
    this->savePlaybackPosition();
    this->video->stop();

    currentChannelIndex = index;
    this->liveData      = channelList[currentChannelIndex];
    nextOffered         = false;
    creditsAt           = -1;
    markedWatched       = false;
    this->video->setTitle(liveData.title);
    this->video->setFavoriteIcon(FavoriteManager::get()->isFavorite(liveData.url));
    // The episode goes to the history, so the series screen offers the right one to continue
    if (liveData.type == 2 && !ProgramConfig::instance().isAdultCategory(liveData.groupTitle))
        HistoryManager::get()->add(liveData);

    this->getAdUrlFromServer([&](const std::string& adUrl) {
        brls::Logger::debug("LiveActivity: adUrl: {}", adUrl);
        if (!adUrl.empty()) {
            this->startAd(adUrl);
        } else {
            this->startLive();
        }
    });
}

bool LiveActivity::currentProgress(double& position, double& duration) {
    position = MPVCore::instance().getDouble("time-pos");
    duration = MPVCore::instance().getDouble("duration");
    return position > 0 && duration > 0;
}

void LiveActivity::savePlaybackPosition() {
    if (tsvitch::isLiveStream(liveData.url, liveData.title)) return;
    double time = 0, length = 0;
    if (!this->currentProgress(time, length)) return;
    auto position = static_cast<int64_t>(time);
    auto duration = static_cast<int64_t>(length);
    // Played to the credits or near the end: watched, and it starts from the beginning next time
    if (position >= duration * tsvitch::WatchedManager::THRESHOLD || (markedWatched && position >= creditsAt))
        tsvitch::WatchedManager::setWatched(liveData.url, true);
    else if (duration - position < 30)
        tsvitch::PlaybackPositionManager::clearPosition(liveData.url);
    else
        tsvitch::PlaybackPositionManager::savePosition(liveData.url, position, duration);
}

void LiveActivity::onVideoEnded() {
    if (this->isAd) return;
    // Watched to the end: next time it starts from the beginning
    if (!tsvitch::isLiveStream(liveData.url, liveData.title)) tsvitch::WatchedManager::setWatched(liveData.url, true);
    if (!seriesPlaylist || currentChannelIndex + 1 >= channelList.size()) return;
    if (!ProgramConfig::instance().getBoolOption(SettingItem::PLAYER_AUTO_NEXT)) return;
    // The question may be on the screen already (it came with the closing credits)
    if (nextDialogOpen && *nextDialogOpen) return;
    this->offerNextEpisode();
}

void LiveActivity::onProgress() {
    if (this->isAd || tsvitch::isLiveStream(liveData.url, liveData.title)) return;
    double position = 0, duration = 0;
    if (!this->currentProgress(position, duration)) return;
    // Most of it was played: watched (it still starts from the beginning only once the player is left)
    if (!markedWatched && position >= duration * tsvitch::WatchedManager::THRESHOLD) {
        markedWatched = true;
        tsvitch::WatchedManager::setWatched(liveData.url, true);
    }
    if (!seriesPlaylist || nextOffered || currentChannelIndex + 1 >= channelList.size()) return;
    if (!ProgramConfig::instance().getBoolOption(SettingItem::PLAYER_AUTO_NEXT)) return;
    if (creditsAt < 0) creditsAt = this->findCreditsStart(duration);
    if (position < creditsAt) return;
    // The closing credits: the episode counts as watched, and the next one is offered (Cancel keeps playing)
    nextOffered = true;
    if (!markedWatched) {
        markedWatched = true;
        tsvitch::WatchedManager::setWatched(liveData.url, true);
    }
    this->offerNextEpisode();
}

double LiveActivity::findCreditsStart(double duration) {
    // Some files carry chapters; one named for the closing credits (in the second half) tells the moment
    auto& mpv     = MPVCore::instance();
    int64_t count = mpv.getInt("chapter-list/count");
    for (int64_t i = count - 1; i >= 0; i--) {
        double time = mpv.getDouble(fmt::format("chapter-list/{}/time", i));
        if (time < duration / 2) break;
        std::string title = mpv.getString(fmt::format("chapter-list/{}/title", i));
        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        bool credits = title == "ed";
        for (const char* word : {"credit", "outro", "ending", "end titles", "jenerik"})
            if (title.find(word) != std::string::npos) credits = true;
        if (credits) {
            brls::Logger::info("LiveActivity: closing credits at {:.0f} s (chapter '{}')", time, title);
            return time;
        }
    }
    static const int before[] = {0, 30, 60, 120};
    int choice = std::clamp(ProgramConfig::instance().getSettingItem(SettingItem::PLAYER_NEXT_AT, 2), 0, 3);
    // "When the episode ends": only the end of the file offers it
    if (before[choice] == 0) return duration + 1;
    // Never in the first half (short videos)
    return std::max(duration - before[choice], duration / 2);
}

void LiveActivity::offerNextEpisode() {
    size_t next  = currentChannelIndex + 1;
    auto open    = std::make_shared<bool>(true);
    nextDialogOpen = open;
    auto* view   = new NextEpisodeView(open, channelList[next].title);
    auto* dialog = new brls::Dialog(view);
    // Closing the dialog in any way (B too) stops the countdown: the view is deleted with it
    dialog->addButton("tsvitch/player/next_episode/play_now"_i18n, [this, next]() { this->switchTo(next); });
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->open();
    this->countDownNextEpisode(open, view, dialog, 10, next);
}

void LiveActivity::countDownNextEpisode(const std::shared_ptr<bool>& open, NextEpisodeView* view,
                                        brls::Dialog* dialog, int seconds, size_t next) {
    view->setSeconds(seconds);
    nextEpisodeDelay = brls::delay(1000, [this, open, view, dialog, seconds, next]() {
        if (!*open) return;
        if (seconds > 1) {
            this->countDownNextEpisode(open, view, dialog, seconds - 1, next);
            return;
        }
        *open = false;
        dialog->close([this, next]() { this->switchTo(next); });
    });
}

void LiveActivity::detectContentType() {
    std::string url   = liveData.url;
    std::string title = liveData.title;
    std::transform(url.begin(), url.end(), url.begin(), ::tolower);
    std::transform(title.begin(), title.end(), title.begin(), ::tolower);

    bool isLiveStream;
    if (url.rfind("file://", 0) == 0) {
        isLiveStream = false;
    } else if (ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) == 1) {
        // Xtream lists say what an item is; a live stream can still report a duration (HLS window, a TS
        // stream with a length), which made the progress bar run on live channels
        isLiveStream = liveData.type == 0;
    } else {
        // M3U lists do not say: the address and the title decide, then the duration mpv finds
        isLiveStream = !(url.find(".mp4") != std::string::npos || url.find(".mkv") != std::string::npos ||
                         url.find(".avi") != std::string::npos || url.find("video") != std::string::npos);
        double duration = MPVCore::instance().duration;
        if (duration > 0)
            isLiveStream = false;
        else if (duration == 0 && MPVCore::instance().isPlaying())
            isLiveStream = true;
    }
    brls::Logger::debug("LiveActivity: Content detected as: {}", isLiveStream ? "LIVE STREAM" : "VIDEO WITH DURATION");

    // The hint above the title: the buttons that change the channel (live TV only), and the skip on the arrows
    std::string hint;
    if (liveData.type == 0 && channelList.size() > 1)
        hint = brls::getStr("tsvitch/player/hint/channels", "\uE0E4", "\uE0E5");
    if (!isLiveStream) {
        if (!hint.empty()) hint += "      ";
        hint += brls::getStr("tsvitch/player/hint/seek", "\uE0ED", "\uE0EE");
    }
    this->video->setOsdHint(hint);

    // Configura l'interfaccia in base al tipo di contenuto
    if (isLiveStream) {
        this->video->setLiveMode();
        this->video->hideVideoProgressSlider();
        brls::Logger::debug("LiveActivity: Configured for live stream mode");
    } else {
        this->video->setVideoMode();
        this->video->showVideoProgressSlider();
        brls::Logger::debug("LiveActivity: Configured for video mode with progress bar");
    }
}

void LiveActivity::onLiveData(std::string url) {
    brls::Logger::debug("Live stream url: {}", url);
    this->getAdUrlFromServer([&](const std::string& adUrl) {
        brls::Logger::debug("LiveActivity: adUrl: {}", adUrl);
        if (!adUrl.empty()) {
            this->video->setUrl(adUrl);
        } else {
            this->startLive();
        }
    });
    return;
}

void LiveActivity::onError(const std::string& error) {
    brls::Logger::error("ERROR request live data: {}", error);
    this->video->showOSD(false);
    this->retryRequestData();
}

void LiveActivity::retryRequestData() {
    brls::cancelDelay(errorDelayIter);
    errorDelayIter = brls::delay(2000, [this]() {
        if (!MPVCore::instance().isPlaying()) static_cast<LiveDataRequest*>(this)->requestData(liveData.url);
    });
}

void LiveActivity::getAdUrlFromServer(std::function<void(const std::string&)> callback) {
    CLIENT::get_ad(
        [callback](const std::string& adUrl, int statusCode) {
            if (statusCode == 200 && !adUrl.empty()) {
                brls::Logger::debug("LiveActivity: adUrl: {}", adUrl);
                if (callback) callback(adUrl);
            } else {
                brls::Logger::error("LiveActivity: Failed to get ad URL, status code: {}", statusCode);
                if (callback) callback("");
            }
        },
        [callback](const std::string& error, int statusCode) {
            brls::Logger::error("LiveActivity: Error getting ad URL: {}, status code: {}", error, statusCode);
            if (callback) callback("");
        });
}

LiveActivity::~LiveActivity() {
    brls::Logger::debug("LiveActivity: delete");
    
    this->savePlaybackPosition();
    brls::cancelDelay(nextEpisodeDelay);
    *alive = false;
    brls::cancelDelay(epgDelay);

    if (this->video) {
        this->video->setOnEndCallback(nullptr);  // Annulla la callback per evitare crash
        this->video->stop();
    }
    brls::cancelDelay(toggleDelayIter);
    brls::cancelDelay(errorDelayIter);
    
    // Pulisci gli eventi in modo sicuro per evitare callback dopo la distruzione
    try {
        if (mpvEventRegistered) {
            MPVCore::instance().getEvent()->unsubscribe(this->tl_event_id);
            mpvEventRegistered = false;
        }
    } catch (const std::exception& e) {
        brls::Logger::warning("LiveActivity: Error unsubscribing MPV event: {}", e.what());
    } catch (...) {
        brls::Logger::warning("LiveActivity: Unknown error unsubscribing MPV event");
    }
    
    try {
        if (customEventRegistered) {
            EventHelper::instance().getCustomEvent()->unsubscribe(this->event_id);
            customEventRegistered = false;
        }
    } catch (const std::exception& e) {
        brls::Logger::warning("LiveActivity: Error unsubscribing custom event: {}", e.what());
    } catch (...) {
        brls::Logger::warning("LiveActivity: Unknown error unsubscribing custom event");
    }
    
    lastIndex = currentChannelIndex;
    if (onCloseCallback && !Intent::isClosing()) onCloseCallback();
}