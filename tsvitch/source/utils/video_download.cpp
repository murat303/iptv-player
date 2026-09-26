#include "utils/video_download.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/views/dialog.hpp>

#include "core/DownloadManager.hpp"
#include "utils/stream_helper.hpp"

using namespace brls::literals;

namespace tsvitch {

void startVideoDownload(const tsvitch::LiveM3u8& channel) {
    // A series is not a video: its episodes are downloaded from its information screen
    if (channel.url.rfind("xtream-series://", 0) == 0) {
        brls::Application::notify("tsvitch/detail/download_series_hint"_i18n);
        return;
    }
    if (tsvitch::isLiveStream(channel.url, channel.title)) {
        tsvitch::showLiveStreamDownloadError();
        return;
    }

    auto& manager = DownloadManager::instance();
    manager.loadDownloads();

    DownloadItem existing;
    if (manager.findByUrl(channel.url, existing)) {
        if (existing.status == DownloadStatus::COMPLETED) {
            brls::Application::notify("tsvitch/download/already"_i18n);
            return;
        }
        if (existing.status == DownloadStatus::DOWNLOADING || existing.status == DownloadStatus::PENDING) {
            brls::Application::notify("tsvitch/download/already_running"_i18n);
            return;
        }
    }

    // The IPTV account allows one connection: a second download would break the first one
    DownloadItem active;
    if (manager.getActiveDownload(active)) {
        auto* dialog = new brls::Dialog(brls::getStr("tsvitch/download/busy_download", active.title));
        dialog->addButton("hints/ok"_i18n, []() {});
        dialog->open();
        return;
    }

    std::string id = manager.startDownload(channel.title, channel.url, channel.logo);
    if (id.empty()) {
        brls::Application::notify("tsvitch/download/start_error"_i18n);
        return;
    }
    brls::Application::notify(brls::getStr("tsvitch/download/started_title", channel.title));
}

}  // namespace tsvitch
