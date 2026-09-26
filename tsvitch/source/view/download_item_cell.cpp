#include "view/download_item_cell.hpp"

#include <algorithm>
#include <cstdio>
#include <borealis/core/i18n.hpp>
#include <fmt/format.h>

#include "utils/image_helper.hpp"

using namespace brls::literals;

namespace {

// "8 dk kaldı" from the bytes still to come and the current speed
std::string timeLeft(size_t remaining, double speed) {
    if (speed < 1) return "";
    auto seconds = static_cast<long long>(static_cast<double>(remaining) / speed);
    if (seconds < 60) return "tsvitch/download/time_left_less"_i18n;
    long long minutes = (seconds + 59) / 60;
    if (minutes < 60) return brls::getStr("tsvitch/download/time_left_m", minutes);
    return brls::getStr("tsvitch/download/time_left_h", minutes / 60, minutes % 60);
}

}  // namespace

DownloadItemCell::DownloadItemCell() { this->inflateFromXMLRes("xml/views/download_item_cell.xml"); }

DownloadItemCell::~DownloadItemCell() { ImageHelper::clear(this->image); }

RecyclingGridItem* DownloadItemCell::create() { return new DownloadItemCell(); }

std::string DownloadItemCell::formatSize(size_t bytes) {
    const double kb = 1024.0, mb = kb * 1024.0, gb = mb * 1024.0;
    std::string text;
    if (bytes >= gb)
        text = fmt::format("{:.2f} GB", bytes / gb);
    else if (bytes >= mb)
        text = fmt::format("{:.1f} MB", bytes / mb);
    else
        text = fmt::format("{:.0f} KB", bytes / kb);
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}

void DownloadItemCell::setDownloadItem(const DownloadItem& item) {
    if (item.id != downloadId || item.imageUrl != imageUrl || item.imagePath != imagePath) {
        downloadId = item.id;
        imageUrl   = item.imageUrl;
        imagePath  = item.imagePath;
        ImageHelper::clear(this->image);
        // The cover saved next to the video works without internet
        std::FILE* cover = imagePath.empty() ? nullptr : std::fopen(imagePath.c_str(), "rb");
        if (cover) {
            std::fclose(cover);
            this->image->setImageFromFile(imagePath);
        } else if (!imageUrl.empty()) {
            ImageHelper::with(this->image)->load(ImageHelper::smallPoster(imageUrl));
        } else {
            this->image->setImageFromRes("pictures/video-card-bg.png");
        }
    }
    this->title->setText(item.title);

    std::string state, info;
    NVGcolor color = nvgRGB(255, 145, 0);
    std::string sizes =
        item.totalSize ? formatSize(item.downloadedSize) + " / " + formatSize(item.totalSize) : formatSize(item.downloadedSize);
    std::string percent = item.totalSize ? fmt::format("%{:.0f}", item.progress) : "";
    switch (item.status) {
        case DownloadStatus::DOWNLOADING: {
            state = "tsvitch/download/status/downloading"_i18n;
            std::vector<std::string> parts;
            if (!percent.empty()) parts.push_back(percent);
            if (item.downloadedSize) parts.push_back(sizes);
            if (item.speed >= 1) parts.push_back(brls::getStr("tsvitch/download/speed", formatSize((size_t)item.speed)));
            if (item.totalSize > item.downloadedSize) {
                std::string left = timeLeft(item.totalSize - item.downloadedSize, item.speed);
                if (!left.empty()) parts.push_back(left);
            }
            for (size_t i = 0; i < parts.size(); i++) info += (i ? "  ·  " : "") + parts[i];
            break;
        }
        case DownloadStatus::PENDING:
            state = "tsvitch/download/status/queued"_i18n;
            info  = item.downloadedSize ? (percent.empty() ? sizes : percent + "  ·  " + sizes) : "";
            break;
        case DownloadStatus::PAUSED:
        case DownloadStatus::CANCELLED:
            state = "tsvitch/download/status/paused"_i18n;
            color = nvgRGB(180, 185, 194);
            info  = item.downloadedSize ? (percent.empty() ? sizes : percent + "  ·  " + sizes) : "";
            break;
        case DownloadStatus::FAILED:
            state = "tsvitch/download/status/failed"_i18n;
            color = nvgRGB(255, 90, 90);
            info  = item.error;
            break;
        case DownloadStatus::COMPLETED:
            state = "tsvitch/download/status/completed"_i18n;
            color = nvgRGB(76, 217, 100);
            info  = formatSize(item.totalSize ? item.totalSize : item.downloadedSize);
            break;
    }
    this->status->setText(state);
    this->status->setTextColor(color);
    this->detail->setText(info);

    bool showBar = item.status != DownloadStatus::COMPLETED && item.totalSize > 0;
    this->bar->setVisibility(showBar ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (showBar) {
        this->barFill->setWidthPercentage(std::clamp(item.progress, 0.0f, 100.0f));
        this->barFill->setBackgroundColor(item.status == DownloadStatus::FAILED ? nvgRGB(255, 90, 90)
                                          : item.status == DownloadStatus::DOWNLOADING ? nvgRGB(255, 145, 0)
                                                                                       : nvgRGB(140, 145, 155));
    }
}

void DownloadItemCell::prepareForReuse() {
    RecyclingGridItem::prepareForReuse();
    downloadId.clear();
    imageUrl.clear();
    imagePath.clear();
}

void DownloadItemCell::cacheForReuse() {
    RecyclingGridItem::cacheForReuse();
    ImageHelper::clear(this->image);
}
