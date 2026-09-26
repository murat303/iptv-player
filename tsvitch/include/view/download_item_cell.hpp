#pragma once

#include <borealis/core/bind.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>

#include "view/recycling_grid.hpp"
#include "core/DownloadManager.hpp"

/// One row of the downloads tab: cover, title, state and progress
class DownloadItemCell : public RecyclingGridItem {
public:
    DownloadItemCell();

    ~DownloadItemCell() override;

    /// Shows a download; the cover is loaded only when the download changes
    void setDownloadItem(const DownloadItem& item);

    const std::string& getDownloadId() const { return downloadId; }

    static RecyclingGridItem* create();

    void prepareForReuse() override;

    void cacheForReuse() override;

    /// "1,2 GB" (the decimal comma of the Turkish text)
    static std::string formatSize(size_t bytes);

private:
    BRLS_BIND(brls::Image, image, "download_item/image");
    BRLS_BIND(brls::Label, title, "download_item/title");
    BRLS_BIND(brls::Label, status, "download_item/status");
    BRLS_BIND(brls::Label, detail, "download_item/detail");
    BRLS_BIND(brls::Box, bar, "download_item/bar");
    BRLS_BIND(brls::Box, barFill, "download_item/bar/fill");

    std::string downloadId;
    std::string imageUrl;
    std::string imagePath;
};
