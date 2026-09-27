#include "fragment/home_discover.hpp"

#include <chrono>

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

#include "api/tmdb.hpp"
#include "core/Catalog.hpp"
#include "utils/activity_helper.hpp"
#include "utils/config_helper.hpp"
#include "view/discover_views.hpp"
#include "view/loading_ring.hpp"

using namespace brls::literals;

namespace {

int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

constexpr int64_t LIST_TIMEOUT_MS = 6000;  // lists that take longer do not hold the page
constexpr int64_t TREND_AGE       = 12 * 3600;
constexpr int64_t POPULAR_AGE     = 24 * 3600;
constexpr int64_t RECOMMEND_AGE   = 30 * 24 * 3600;

}  // namespace

HomeDiscover::HomeDiscover() {
    this->inflateFromXMLRes("xml/fragment/home_discover.xml");

    auto isAlive        = alive;
    catalogSubscription = Catalog::instance().getChangedEvent()->subscribe([this, isAlive]() {
        if (!*isAlive) return;
        dirty     = true;
        dirtySoon = true;
    });
    tmdbSubscription = tsvitch::TmdbService::instance().getChangedEvent()->subscribe([this, isAlive]() {
        if (!*isAlive) return;
        this->updateStatus();
        // While the catalogue's TMDB data comes in, the page grows at most every 15 s (see draw)
        dirty = true;
    });
    settingsSubscription = tsvitch::discover::getChangedEvent()->subscribe([this, isAlive]() {
        if (!*isAlive) return;
        // The settings screen is still open: the page is built again when it is seen (see draw)
        dirty     = true;
        dirtySoon = true;
    });
}

HomeDiscover::~HomeDiscover() {
    *alive = false;
    if (scheduled) brls::cancelDelay(scheduled);
    Catalog::instance().getChangedEvent()->unsubscribe(catalogSubscription);
    tsvitch::TmdbService::instance().getChangedEvent()->unsubscribe(tmdbSubscription);
    tsvitch::discover::getChangedEvent()->unsubscribe(settingsSubscription);
}

brls::View* HomeDiscover::create() { return new HomeDiscover(); }

void HomeDiscover::onShow() {
    visible = true;
    Catalog::instance().ensureLoaded();
    tsvitch::TmdbService::instance().refresh();
    this->updateStatus();
    // Scrolling through the sidebar shows and hides the tab quickly: the page is built once the tab stays
    if (dirty || content->getChildren().empty()) this->schedule(250);
}

void HomeDiscover::onHide() {
    visible = false;
    if (scheduled) {
        brls::cancelDelay(scheduled);
        scheduled = 0;
    }
}

void HomeDiscover::schedule(int delayMs) {
    if (scheduled) brls::cancelDelay(scheduled);
    auto isAlive = alive;
    scheduled    = brls::delay(delayMs, [this, isAlive]() {
        if (!*isAlive) return;
        scheduled = 0;
        if (visible) this->requestData();
    });
}

bool HomeDiscover::canRebuild() {
    // A screen above (a detail, the player) remembers a card of this page to focus again: it must stay
    auto stack = brls::Application::getActivitiesStack();
    if (stack.empty() || stack.back() != this->getParentActivity()) return false;
    for (brls::View* view = brls::Application::getCurrentFocus(); view; view = view->getParent())
        if (view == this) return false;
    return true;
}

void HomeDiscover::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                        brls::FrameContext* ctx) {
    AttachedView::draw(vg, x, y, width, height, style, ctx);
    if (!dirty || !visible || scheduled || waitingBuild) return;
    if (!dirtySoon && steadyMs() - lastBuildMs < 15000) return;
    // Built on the next frames, never while this one is drawn, and never under the user's focus
    if (this->canRebuild()) this->schedule(dirtySoon ? 100 : 300);
}

brls::View* HomeDiscover::getDefaultFocus() {
    if (content->getChildren().empty()) return nullptr;
    return content->getDefaultFocus();
}

void HomeDiscover::showMessage(const std::string& text, bool loading) {
    bool shown = loading || !text.empty();
    messageBox->setVisibility(shown ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    messageRing->setVisibility(loading ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    messageLabel->setText(text);
}

void HomeDiscover::updateStatus() {
    auto& tmdb = tsvitch::TmdbService::instance();
    std::string text;
    if (tmdb.keyRejected()) {
        text = "tsvitch/discover/status/rejected"_i18n;
    } else if (tmdb.enabled()) {
        size_t done = 0, total = 0;
        tmdb.progress(done, total);
        if (total > 0 && done < total) text = brls::getStr("tsvitch/discover/status/progress", done * 100 / total);
    }
    statusLabel->setText(text);
    statusLabel->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void HomeDiscover::requestData() {
    auto& catalog = Catalog::instance();
    // This request answers the changes so far; later ones set the flags again
    dirty     = false;
    dirtySoon = false;
    if (ProgramConfig::instance().getSettingItem(SettingItem::IPTV_MODE, 0) != 1) {
        content->clearViews();
        this->showMessage("tsvitch/discover/need_xtream"_i18n, false);
        return;
    }
    if (!catalog.isLoaded()) {
        // The catalogue's changed event asks again
        if (content->getChildren().empty()) this->showMessage("", true);
        return;
    }
    if (catalog.items(1).empty() && catalog.items(2).empty()) {
        content->clearViews();
        this->showMessage("tsvitch/discover/no_lists"_i18n, true);
        return;
    }

    uint64_t current = ++serial;
    inputs           = {};
    inputs.seeds     = tsvitch::discover::seeds(6);
    waiting          = 1;
    waitingBuild     = true;
    auto isAlive     = alive;
    auto arrived     = [this, isAlive, current]() {
        if (!*isAlive || current != serial) return;
        if (!waitingBuild) {
            // Came after the page was built without it: it shows the next time
            dirty = true;
            return;
        }
        if (--waiting == 0) {
            waitingBuild = false;
            this->rebuild();
        }
    };
    auto& tmdb = tsvitch::TmdbService::instance();
    if (tmdb.enabled()) {
        waiting++;
        tmdb.list("trending/all/week", 2, TREND_AGE,
                  [this, isAlive, current, arrived](const std::vector<tsvitch::TmdbRef>& refs) {
                      if (!*isAlive || current != serial) return;
                      inputs.trending = refs;
                      arrived();
                  });
        for (int type : {1, 2}) {
            waiting++;
            tmdb.list(type == 1 ? "movie/popular" : "tv/popular", 5, POPULAR_AGE,
                      [this, isAlive, current, type, arrived](const std::vector<tsvitch::TmdbRef>& refs) {
                          if (!*isAlive || current != serial) return;
                          (type == 1 ? inputs.popularMovies : inputs.popularSeries) = refs;
                          arrived();
                      });
        }
        for (size_t i = 0; i < inputs.seeds.size(); i++) {
            const auto& seed = inputs.seeds[i];
            std::string path =
                std::string(seed.type == 1 ? "movie/" : "tv/") + std::to_string(seed.tmdb) + "/recommendations";
            waiting++;
            tmdb.list(path, 1, RECOMMEND_AGE,
                      [this, isAlive, current, i, arrived](const std::vector<tsvitch::TmdbRef>& refs) {
                          if (!*isAlive || current != serial) return;
                          if (i < inputs.seeds.size()) inputs.seeds[i].recommendations = refs;
                          arrived();
                      });
        }
    }
    arrived();
    if (waitingBuild) {
        if (content->getChildren().empty()) this->showMessage("", true);
        brls::delay(LIST_TIMEOUT_MS, [this, isAlive, current]() {
            if (!*isAlive || current != serial || !waitingBuild) return;
            waitingBuild = false;
            this->rebuild();
        });
    }
}

void HomeDiscover::rebuild() {
    if (!this->canRebuild() && !content->getChildren().empty()) {
        dirty = true;
        return;
    }
    auto started = steadyMs();
    auto page    = tsvitch::discover::build(inputs);
    lastBuildMs  = steadyMs();
    dirty        = false;
    dirtySoon    = false;

    content->clearViews();
    scroll->setContentOffsetY(0, false);
    auto isAlive  = alive;
    auto openItem = [this, isAlive](const tsvitch::LiveM3u8& item) {
        if (*isAlive) this->openItem(item);
    };
    auto openCollection = [this, isAlive](const tsvitch::discover::Collection& collection) {
        if (*isAlive) this->openCollection(collection);
    };
    if (page.hero.valid) content->addView(new DiscoverHero(page.hero, openItem));
    size_t shelves = page.shelves.size();
    for (auto& shelf : page.shelves) content->addView(new DiscoverShelfView(std::move(shelf), openItem, openCollection));

    auto& tmdb = tsvitch::TmdbService::instance();
    if (!tmdb.enabled() && shelves > 0) {
        // Without TMDB the movie genres, themes and collections are missing: say why at the end of the page
        auto* note = new brls::Label();
        note->setFontSize(16);
        note->setMarginTop(30);
        note->setMarginLeft(30);
        note->setMarginRight(30);
        note->setTextColor(brls::Application::getTheme().getColor("font/grey"));
        note->setText(tmdb.hasKey() ? "tsvitch/discover/tmdb_off"_i18n : "tsvitch/discover/tmdb_missing"_i18n);
        content->addView(note);
    }
    if (content->getChildren().empty())
        this->showMessage("tsvitch/discover/empty"_i18n, false);
    else
        this->showMessage("", false);
    this->updateStatus();
    brls::Logger::info("Discover: {} shelves built in {} ms", shelves, lastBuildMs - started);
}

void HomeDiscover::openItem(const tsvitch::LiveM3u8& item) {
    auto isAlive = alive;
    Intent::openXtreamDetail(item, [this, isAlive]() {
        if (*isAlive) this->refreshCards();
    });
}

void HomeDiscover::openCollection(const tsvitch::discover::Collection& collection) {
    auto isAlive = alive;
    Intent::openDiscoverList(collection.id, collection.title, [this, isAlive]() {
        if (*isAlive) this->refreshCards();
    });
}

void HomeDiscover::refreshCards() {
    for (auto* child : content->getChildren())
        if (auto* shelf = dynamic_cast<DiscoverShelfView*>(child)) shelf->refreshWatchStates();
    // What was watched moves between the shelves the next time the tab is shown
    dirty = true;
}
