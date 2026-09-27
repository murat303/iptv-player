#pragma once

#include <functional>
#include <string>
#include <vector>

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

#include "api/tsvitch/result/home_live_result.h"

namespace brls {
class Box;
class Label;
}
class RecyclingGrid;
class CustomButton;

/// Every title of a collection of the discovery screen (a genre, a theme, a studio, an award...) in a poster grid
class DiscoverListActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/discover_list.xml");

    DiscoverListActivity(std::string collectionId, std::string title, std::function<void()> onClose = nullptr);
    ~DiscoverListActivity() override;

    void onContentAvailable() override;

private:
    void show(size_t focus = 0);
    void pickSort();
    void updateSortLabel();

    std::string collectionId;
    std::string title;
    std::function<void()> onClose;
    std::vector<tsvitch::LiveM3u8> items;  // in the collection's own order
    int sortMode = 0;

    BRLS_BIND(brls::Box, root, "discover/list/root");
    BRLS_BIND(brls::Label, titleLabel, "discover/list/title");
    BRLS_BIND(brls::Label, countLabel, "discover/list/count");
    BRLS_BIND(CustomButton, sortButton, "discover/list/sort");
    BRLS_BIND(brls::Label, sortLabel, "discover/list/sort/label");
    BRLS_BIND(RecyclingGrid, grid, "discover/list/grid");
};
