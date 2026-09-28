#include "activity/trailer_activity.hpp"

#include <fstream>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <cpr/cpr.h>
#include <fmt/format.h>

#include "api/youtube.hpp"
#include "utils/config_helper.hpp"
#include "utils/shader_helper.hpp"
#include "view/mpv_core.hpp"
#include "view/video_view.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

using namespace brls::literals;

/// At most max letters of a UTF-8 text, with "…" when it was longer
static std::string shorten(const std::string& text, size_t max) {
    size_t letters = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (letters++ == max) return text.substr(0, i) + "…";
    }
    return text;
}

TrailerActivity::TrailerActivity(std::string title, std::vector<tsvitch::TmdbVideo> videos)
    : title(std::move(title)), videos(std::move(videos)) {
    brls::Logger::debug("TrailerActivity: create: {}", this->title);
    ShaderHelper::instance().clearShader(false);
    // The catalogue's TMDB data waits while a video plays
    tsvitch::TmdbService::instance().setPaused(true);
}

void TrailerActivity::onContentAvailable() {
    video = dynamic_cast<VideoView*>(this->getView("video"));

    video->registerAction("", brls::BUTTON_B, [this](...) {
        if (video->isOSDLock() || (video->getTvControlMode() && video->isOSDShown()))
            video->toggleOSD();
        else
            this->close();
        return true;
    });
    video->hideSubtitleSetting();
    video->hideVideoRelatedSetting();
    video->hideBottomLineSetting();
    video->hideHighlightLineSetting();
    video->disableCloseOnEndOfFile();
    video->setFullscreenIcon(true);
    // A trailer is no favorite
    if (brls::View* favorite = video->getFavoriteIcon()) favorite->getParent()->setVisibility(brls::Visibility::GONE);
    video->setTitle(title);
    video->setStatusLabelLeft("");
    video->setVideoMode();
    video->showVideoProgressSlider();
    // The end of the trailer closes the player, once the view has handled the event
    auto alive = this->alive;
    video->setOnEndCallback([this, alive]() {
        brls::sync([this, alive]() {
            if (*alive) this->close();
        });
    });

    video->showLoading();
    this->resolve(0);
}

void TrailerActivity::resolve(size_t index) {
    if (index >= videos.size()) {
        video->hideLoading();
        auto alive   = this->alive;
        auto* dialog = new brls::Dialog("tsvitch/detail/trailer_error"_i18n);
        dialog->addButton("hints/back"_i18n, [this, alive]() {
            if (*alive) this->close();
        });
        dialog->open();
        return;
    }
    // 1080p on the TV; 720p handheld (the screen has no more) and without hardware decoding
    int maxHeight = MPVCore::HARDWARE_DEC ? 1080 : 720;
#ifdef __SWITCH__
    if (appletGetOperationMode() != AppletOperationMode_Console) maxHeight = 720;
#endif
    std::string id = videos[index].key;
    auto alive     = this->alive;
    cpr::async([this, alive, id, index, maxHeight]() {
        tsvitch::youtube::Stream stream;
        std::string error;
        bool ok = tsvitch::youtube::resolve(id, maxHeight, stream, error);
        brls::sync([this, alive, index, ok, stream]() {
            if (!*alive) return;
            if (ok)
                this->play(stream, videos[index]);
            else
                this->resolve(index + 1);
        });
    });
}

void TrailerActivity::play(const tsvitch::youtube::Stream& stream, const tsvitch::TmdbVideo& trailer) {
    // Under the title: the trailer's name ("Türkçe Altyazılı 1. Fragman") and the skip keys; a long name would
    // make the line scroll
    std::string hint = brls::getStr("tsvitch/player/hint/seek", "\uE0ED", "\uE0EE");
    if (!trailer.name.empty()) hint = shorten(trailer.name, 60) + "      " + hint;
    video->setOsdHint(hint);
    // The playlist goes to a file in the app's folder. The path has no "sdmc:" in front, which FFmpeg would take
    // for a protocol it does not know.
    std::string path = ProgramConfig::instance().getConfigDir() + "/trailer.m3u8";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << stream.playlist;
    file.close();
    if (!file) {
        brls::Logger::error("Trailer: cannot write {}", path);
        this->resolve(videos.size());
        return;
    }
    // For this file only:
    // - FFmpeg's HLS demuxer, never mpv's playlist parser (it would play the picture's playlist alone, without the
    //   sound), allowed to open https from a local file
    // - YouTube's user agent and header
    // - no reconnect after a response without a size: the app lets FFmpeg reconnect for IPTV servers, but YouTube
    //   sends its segments chunked, so the end of each one looked like a cut connection
    auto quoted = [](const std::string& value) { return fmt::format("%{}%{}", value.size(), value); };
    std::string options =
        "demuxer=lavf,demuxer-lavf-format=hls,demuxer-lavf-o=" +
        quoted("protocol_whitelist=[file,http,https,tcp,tls,crypto,data]") +
        ",user-agent=" + quoted(tsvitch::youtube::PLAYER_USER_AGENT) +
        ",http-header-fields=" + quoted(tsvitch::youtube::PLAYER_HEADER) +
        ",stream-lavf-o=" + quoted("reconnect=1,reconnect_on_network_error=1,reconnect_delay_max=5");
    video->setUrlWithOptions(path, options);
}

void TrailerActivity::close() {
    if (closing) return;
    closing = true;
    brls::Application::popActivity();
}

TrailerActivity::~TrailerActivity() {
    brls::Logger::debug("TrailerActivity: delete");
    *alive = false;
    if (video) {
        video->setOnEndCallback(nullptr);
        video->stop();
    }
    tsvitch::TmdbService::instance().setPaused(false);
}
