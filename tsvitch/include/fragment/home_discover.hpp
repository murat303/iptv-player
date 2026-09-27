#pragma once

#include <memory>

#include <borealis/core/application.hpp>
#include <borealis/core/bind.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include "utils/discover.hpp"
#include "view/auto_tab_frame.hpp"

class LoadingRing;
class ProgressLine;

/// The discovery tab: a big title of the week, shelves of titles (continue watching, trending, for you, new...)
/// and of collections (genres, themes, studios, awards, decades, film series), all from the catalogue the provider's
/// lists already gave. The page is built when the tab is shown and new data never rebuilds it under the user's
/// focus: it waits until the focus is outside it.
class HomeDiscover : public AttachedView {
public:
    HomeDiscover();
    ~HomeDiscover() override;

    static brls::View* create();

    void onShow() override;
    void onHide() override;

    brls::View* getDefaultFocus() override;

    // A page waiting for new data is built again once it is on screen and nobody uses it
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    void schedule(int delayMs);
    void requestData();
    void rebuild();
    void updateStatus();
    void showMessage(const std::string& text, bool loading);
    // The page may change now: this tab is on top and the focus is not in it
    bool canRebuild();
    void openItem(const tsvitch::LiveM3u8& item);
    void openCollection(const tsvitch::discover::Collection& collection);
    void refreshCards();

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool visible        = false;
    bool dirty          = true;
    bool dirtySoon      = false;  // the settings or the lists changed: rebuild as soon as possible
    bool waitingBuild   = false;  // the lists asked for have not all come back
    int waiting         = 0;
    uint64_t serial     = 0;
    int64_t lastBuildMs = 0;
    size_t scheduled    = 0;  // brls::delay id (0: none)
    tsvitch::discover::Inputs inputs;
    brls::Event<>::Subscription catalogSubscription, tmdbSubscription, settingsSubscription;

    BRLS_BIND(brls::ScrollingFrame, scroll, "discover/scroll");
    BRLS_BIND(brls::Box, content, "discover/content");
    BRLS_BIND(brls::Box, statusBox, "discover/status");
    BRLS_BIND(LoadingRing, statusRing, "discover/status/ring");
    BRLS_BIND(brls::Label, statusLabel, "discover/status/label");
    BRLS_BIND(ProgressLine, statusBar, "discover/status/bar");
    BRLS_BIND(brls::Box, messageBox, "discover/message");
    BRLS_BIND(LoadingRing, messageRing, "discover/message/ring");
    BRLS_BIND(brls::Label, messageLabel, "discover/message/label");
};
