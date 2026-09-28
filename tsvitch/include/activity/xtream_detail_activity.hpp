#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

#include "api/tsvitch/result/home_live_result.h"
#include "api/tsvitch/result/xtream_detail.h"
#include "api/tmdb.hpp"

namespace brls {
class Box;
class Image;
class Label;
}  // namespace brls
class CustomButton;
class ProgressLine;
class RecyclingGrid;
class TextBox;

/// Information screen of an Xtream movie or series: pictures, plot, cast, play/resume buttons and,
/// for a series, a season picker with the episodes of the chosen season
class XtreamDetailActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/xtream_detail.xml");

    // onClose: called when the screen is left
    explicit XtreamDetailActivity(const tsvitch::LiveM3u8& item, std::function<void()> onClose = nullptr);

    ~XtreamDetailActivity() override;

    void onContentAvailable() override;

private:
    // Asks the server for the details (again after an error)
    void loadDetail();

    // Shows the details; fields the server leaves empty keep what the list already knew
    void showDetail(const tsvitch::XtreamDetail& update);

    // focusEpisode: the row the list scrolls to and focuses when it gets the focus
    void showSeason(size_t index, size_t focusEpisode = 0);

    // One button per season (the selected one highlighted)
    void buildSeasonButtons();

    void updateSeasonButtons();

    // Play / resume labels from the saved playback positions and the watch history
    void updatePlayButtons();

    void play(bool fromStart);

    // Y marks the movie, or on a series the episode of the play button, as watched or not (the hint says which)
    void registerWatchedAction();

    // What Y marks outside the episode list (empty: nothing yet)
    std::string watchedTarget() const;

    void playEpisode(size_t season, size_t episode);

    void toggleFavorite();

    void updateFavoriteLabel();

    // TMDB's rating, cast and similar titles, once they come
    void requestTmdb();
    void showTmdb(int id, const tsvitch::TmdbDetails& tmdb);

    tsvitch::LiveM3u8 item;
    std::function<void()> onClose;
    tsvitch::XtreamDetail detail;
    bool isSeries        = false;
    size_t currentSeason = 0;
    size_t resumeSeason  = 0;
    size_t resumeEpisode = 0;
    // The episode watched last (from the history), before "watched to the end" moves on to the next one
    bool hasLastWatched = false;
    size_t lastSeason   = 0;
    size_t lastEpisode  = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::vector<std::pair<CustomButton*, brls::Label*>> seasonButtons;
    // What TMDB told, kept when the provider's details come later
    float tmdbVote = 0;
    int tmdbVotes  = 0;
    std::string tmdbOverview, tmdbDirectors, tmdbCast;

    BRLS_BIND(brls::Image, backdrop, "detail/backdrop");
    BRLS_BIND(brls::Image, poster, "detail/poster");
    BRLS_BIND(ProgressLine, posterProgress, "detail/poster/progress");
    BRLS_BIND(brls::Box, watchedChip, "detail/watched");
    BRLS_BIND(brls::Label, seasonPrev, "detail/seasons/prev");
    BRLS_BIND(brls::Label, seasonNext, "detail/seasons/next");
    BRLS_BIND(brls::Label, facts, "detail/facts");
    BRLS_BIND(TextBox, title, "detail/title");
    BRLS_BIND(brls::Label, subtitle, "detail/subtitle");
    BRLS_BIND(brls::Box, ratingBox, "detail/rating/box");
    BRLS_BIND(brls::Label, rating, "detail/rating");
    BRLS_BIND(brls::Label, meta, "detail/meta");
    BRLS_BIND(CustomButton, playButton, "detail/play");
    BRLS_BIND(brls::Label, playLabel, "detail/play/label");
    BRLS_BIND(CustomButton, restartButton, "detail/restart");
    BRLS_BIND(CustomButton, favoriteButton, "detail/favorite");
    BRLS_BIND(brls::Label, favoriteLabel, "detail/favorite/label");
    BRLS_BIND(CustomButton, downloadButton, "detail/download");
    BRLS_BIND(TextBox, plot, "detail/plot");
    BRLS_BIND(TextBox, director, "detail/director");
    BRLS_BIND(TextBox, cast, "detail/cast");
    BRLS_BIND(brls::Box, similarBox, "detail/similar");
    BRLS_BIND(brls::Box, seriesBox, "detail/series");
    BRLS_BIND(brls::Box, seasonsBox, "detail/seasons");
    BRLS_BIND(RecyclingGrid, episodes, "detail/episodes");
};
