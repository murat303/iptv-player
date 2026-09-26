#pragma once

#include <chrono>
#include <cstdint>
#include <borealis/core/bind.hpp>
#include <borealis/core/box.hpp>

#include "core/DownloadManager.hpp"

class RecyclingGrid;
class DownloadDataSource;

/// Downloads tab: every download with its state; A opens the actions of a download (pause, continue, play, delete)
class HomeDownloads : public brls::Box {
public:
    HomeDownloads();

    ~HomeDownloads() override;

    static brls::View* create();

    // Looks at the download list twice a second while the tab is on screen
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    void onDownloadSelected(size_t index);

private:
    // Shows the current list: rows that stay are updated in place, otherwise the list is rebuilt
    void refreshList();

    void confirmDelete(const DownloadItem& item);

    void play(const DownloadItem& item);

    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/downloads/recyclingGrid");
    BRLS_BIND(brls::Box, emptyBox, "home/downloads/empty");

    DownloadDataSource* dataSource = nullptr;  // given to the grid
    uint64_t shownVersion          = 0;
    std::chrono::steady_clock::time_point lastCheck;
};
