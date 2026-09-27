#include "activity/xtream_detail_activity.hpp"

#include <cctype>
#include <cstdio>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <borealis/core/box.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>

#include "core/FavoriteManager.hpp"
#include "core/HistoryManager.hpp"
#include "tsvitch.h"
#include "utils/activity_helper.hpp"
#include "utils/config_helper.hpp"
#include "utils/image_helper.hpp"
#include "utils/playback_position_manager.hpp"
#include "utils/watched_manager.hpp"
#include "view/custom_button.hpp"
#include "view/grid_dropdown.hpp"
#include "view/progress_line.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "utils/text_fold.hpp"
#include "utils/video_download.hpp"
#include "core/Catalog.hpp"
#include "core/TmdbStore.hpp"
#include "utils/discover.hpp"
#include "view/discover_views.hpp"

using namespace brls::literals;

namespace {

const std::string SERIES_SCHEME = "xtream-series://";

/// A TMDB picture in the given size ("w342" for posters, "w780" for backdrops); other urls stay as they are
std::string tmdbSize(const std::string& url, const std::string& size) {
    static const std::string tmdb = "image.tmdb.org/t/p/";
    auto start                    = url.find(tmdb);
    if (start == std::string::npos) return url;
    start += tmdb.size();
    auto end = url.find('/', start);
    if (end == std::string::npos) return url;
    return url.substr(0, start) + size + url.substr(end);
}

/// "02:15:08" -> "2 h 15 min", "00:44:27" -> "44 min"
std::string formatDuration(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return "";
    if (h > 0) return brls::getStr("tsvitch/detail/hours_minutes", h, m);
    if (m > 0) return brls::getStr("tsvitch/detail/minutes", m);
    return "";
}

/// "00:44:27" in seconds (0 when the server sent something else)
int64_t durationSeconds(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;
    return h * 3600 + m * 60 + s;
}

/// Playback position as "1:02:15" or "12:34"
std::string formatPosition(int64_t seconds) {
    int64_t h = seconds / 3600, m = seconds % 3600 / 60, s = seconds % 60;
    return h > 0 ? fmt::format("{}:{:02}:{:02}", h, m, s) : fmt::format("{}:{:02}", m, s);
}

std::string joinParts(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& part : parts) {
        if (part.empty()) continue;
        if (!out.empty()) out += "  ·  ";
        out += part;
    }
    return out;
}

}  // namespace

/**
 * Title of an episode without what the row already shows: many servers name episodes like
 * "Series (2026) TR - S01E03 - 3. Bölüm". The series name, the SxxEyy code and a bare
 * "3. Bölüm"/"Episode 3" are dropped, leaving "Episode 3" or the real episode title.
 */
static std::string episodeTitle(const std::string& seriesTitle, const tsvitch::XtreamEpisode& episode) {
    std::string text = episode.item.title;
    auto trimSeparators = [](std::string& t) {
        size_t start = t.find_first_not_of(" -:|.\t");
        t            = start == std::string::npos ? "" : t.substr(start);
        size_t end   = t.find_last_not_of(" -:|\t");
        t            = end == std::string::npos ? "" : t.substr(0, end + 1);
    };
    if (!seriesTitle.empty() && text.rfind(seriesTitle, 0) == 0) text = text.substr(seriesTitle.size());
    trimSeparators(text);
    // SxxEyy code
    if (text.size() >= 4 && (text[0] == 'S' || text[0] == 's') && isdigit((unsigned char)text[1])) {
        size_t i = 1;
        while (i < text.size() && isdigit((unsigned char)text[i])) i++;
        if (i < text.size() && (text[i] == 'E' || text[i] == 'e')) {
            i++;
            while (i < text.size() && isdigit((unsigned char)text[i])) i++;
            text = text.substr(i);
            trimSeparators(text);
        }
    }
    // A bare episode number ("3. Bölüm", "Bölüm 3", "Episode 3") says nothing new
    std::string folded = tsvitch::foldForSearch(text);
    std::string digits, words;
    for (char c : folded) (isdigit((unsigned char)c) ? digits : words) += c;
    trimSeparators(words);
    bool bare = words.empty() || words == "bolum" || words == "episode" || words == "ep" || words == "b";
    if (text.empty() || (bare && !digits.empty())) return brls::getStr("tsvitch/detail/episode", episode.number);
    return episode.number > 0 ? brls::getStr("tsvitch/detail/episode_title", episode.number, text) : text;
}

/// Row of the episode list
class EpisodeCell : public RecyclingGridItem {
public:
    EpisodeCell() { this->inflateFromXMLRes("xml/views/episode_cell.xml"); }

    // onWatchedChanged: Y marked the episode (the play button above follows)
    void setEpisode(const std::string& seriesTitle, const tsvitch::XtreamEpisode& episode,
                    std::function<void()> onWatchedChanged) {
        this->item             = episode.item;
        this->length           = durationSeconds(episode.duration);
        this->onWatchedChanged = std::move(onWatchedChanged);
        title->setText(episodeTitle(seriesTitle, episode));
        std::string rating = episode.item.rating > 0 ? fmt::format("★ {:.1f}", episode.item.rating) : "";
        meta->setText(joinParts({formatDuration(episode.duration), rating, episode.airDate}));
        plot->setText(episode.plot);
        ImageHelper::with(still)->load(episode.item.logo);
        this->showWatchState();
    }

    void prepareForReuse() override { still->setImageFromRes("pictures/video-card-bg.png"); }

    void cacheForReuse() override { ImageHelper::clear(still); }

    static RecyclingGridItem* create() { return new EpisodeCell(); }

    const tsvitch::LiveM3u8& getItem() const { return item; }

private:
    // Where the episode was left (time and bar under the still), a check once watched, and Y to mark it
    void showWatchState() {
        bool watched     = tsvitch::WatchedManager::isWatched(item.url);
        int64_t position = 0, duration = 0;
        bool started     = tsvitch::PlaybackPositionManager::getProgress(item.url, position, duration);
        if (duration <= 0) duration = length;
        if (started) {
            resumeLabel->setText(formatPosition(position));
            resume->setVisibility(brls::Visibility::VISIBLE);
        } else {
            resume->setVisibility(brls::Visibility::GONE);
        }
        watchedIcon->setVisibility(watched ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        float part = started && duration > 0 ? static_cast<float>(position) / duration : watched ? 1.0f : -1.0f;
        if (part >= 0) progress->setProgress(part);
        progress->setVisibility(part >= 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->registerAction(watched ? "tsvitch/detail/mark_unwatched"_i18n : "tsvitch/detail/mark_watched"_i18n,
                             brls::BUTTON_Y, [this](brls::View*) {
                                 tsvitch::WatchedManager::setWatched(item.url,
                                                                     !tsvitch::WatchedManager::isWatched(item.url));
                                 this->showWatchState();
                                 brls::Application::getGlobalHintsUpdateEvent()->fire();
                                 if (onWatchedChanged) onWatchedChanged();
                                 return true;
                             });
    }

    tsvitch::LiveM3u8 item;
    int64_t length = 0;
    std::function<void()> onWatchedChanged;
    BRLS_BIND(brls::Image, still, "episode/still");
    BRLS_BIND(brls::Box, resume, "episode/resume");
    BRLS_BIND(brls::Label, resumeLabel, "episode/resume/label");
    BRLS_BIND(SVGImage, watchedIcon, "episode/watched");
    BRLS_BIND(ProgressLine, progress, "episode/progress");
    BRLS_BIND(brls::Label, title, "episode/title");
    BRLS_BIND(brls::Label, meta, "episode/meta");
    BRLS_BIND(TextBox, plot, "episode/plot");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    EpisodeDataSource(std::string seriesTitle, std::vector<tsvitch::XtreamEpisode> episodes,
                      std::function<void(size_t)> onSelect, std::function<void()> onWatchedChanged)
        : seriesTitle(std::move(seriesTitle)),
          episodes(std::move(episodes)),
          onSelect(std::move(onSelect)),
          onWatchedChanged(std::move(onWatchedChanged)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell = (EpisodeCell*)recycler->dequeueReusableCell("Cell");
        cell->setEpisode(seriesTitle, episodes[index], onWatchedChanged);
        return cell;
    }

    size_t getItemCount() override { return episodes.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        if (onSelect) onSelect(index);
    }

    void clearData() override { episodes.clear(); }

private:
    std::string seriesTitle;
    std::vector<tsvitch::XtreamEpisode> episodes;
    std::function<void(size_t)> onSelect;
    std::function<void()> onWatchedChanged;
};

XtreamDetailActivity::XtreamDetailActivity(const tsvitch::LiveM3u8& item, std::function<void()> onClose)
    : item(item), onClose(std::move(onClose)) {
    isSeries = item.url.rfind(SERIES_SCHEME, 0) == 0;
}

XtreamDetailActivity::~XtreamDetailActivity() {
    alive->store(false);
    ImageHelper::clear(poster);
    ImageHelper::clear(backdrop);
    if (onClose && !Intent::isClosing()) onClose();
}

void XtreamDetailActivity::onContentAvailable() {
    episodes->registerCell("Cell", []() { return EpisodeCell::create(); });

    playButton->registerClickAction([this](brls::View*) {
        this->play(false);
        return true;
    });
    restartButton->registerClickAction([this](brls::View*) {
        this->play(true);
        return true;
    });
    favoriteButton->registerClickAction([this](brls::View*) {
        this->toggleFavorite();
        return true;
    });
    downloadButton->registerClickAction([this](brls::View*) {
        tsvitch::startVideoDownload(item);
        return true;
    });
    for (CustomButton* button :
         std::initializer_list<CustomButton*>{playButton, restartButton, favoriteButton, downloadButton})
        button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
    this->getContentView()->registerAction("hints/toggle_favorite"_i18n, brls::BUTTON_X, [this](brls::View*) {
        this->toggleFavorite();
        return true;
    });

    // ZR downloads the movie, or the focused episode of a series
    this->getContentView()->registerAction("tsvitch/download/action"_i18n, brls::BUTTON_RT, [this](brls::View*) {
        if (!isSeries) {
            tsvitch::startVideoDownload(item);
        } else if (auto* cell = dynamic_cast<EpisodeCell*>(episodes->getFocusedItem())) {
            // In the downloads tab the episode shows with the series name and poster
            tsvitch::LiveM3u8 episode = cell->getItem();
            if (!item.logo.empty()) episode.logo = item.logo;
            if (episode.title.find(item.title) == std::string::npos) episode.title = item.title + " - " + episode.title;
            tsvitch::startVideoDownload(episode);
        } else {
            brls::Application::notify("tsvitch/detail/download_pick_episode"_i18n);
        }
        return true;
    });

    if (isSeries) {
        // L and R change the season from anywhere on the page, also from inside the episode list; they are
        // offered once the series turns out to have more than one season
        auto changeSeason = [this](int step, brls::FocusDirection direction) {
            return [this, step, direction](brls::View*) {
                brls::View* focus = brls::Application::getCurrentFocus();
                bool atEdge = step < 0 ? currentSeason == 0 : currentSeason + 1 >= detail.seasons.size();
                if (atEdge) {
                    if (focus) focus->shakeHighlight(direction);
                    return true;
                }
                size_t next = currentSeason + step;
                bool onChip = false;
                for (const auto& chip : seasonButtons) onChip = onChip || chip.first == focus;
                this->showSeason(next);
                // On a season chip the focus moves along to the new season's chip
                if (onChip && next < seasonButtons.size()) brls::Application::giveFocus(seasonButtons[next].first);
                return true;
            };
        };
        // Hidden hints: the L and R glyphs beside the season buttons show them (the footer needs the room)
        this->getContentView()->registerAction("tsvitch/detail/previous_season"_i18n, brls::BUTTON_LB,
                                               changeSeason(-1, brls::FocusDirection::LEFT), true);
        this->getContentView()->registerAction("tsvitch/detail/next_season"_i18n, brls::BUTTON_RB,
                                               changeSeason(1, brls::FocusDirection::RIGHT), true);
        seasonPrev->setText("\uE0E4");
        seasonNext->setText("\uE0E5");
        this->getContentView()->setActionAvailable(brls::BUTTON_LB, false);
        this->getContentView()->setActionAvailable(brls::BUTTON_RB, false);

        downloadButton->setVisibility(brls::Visibility::GONE);
        // Room for the episodes: a shorter plot and cast
        plot->setMaxRows(3);
        cast->setMaxRows(1);
        seriesBox->setVisibility(brls::Visibility::VISIBLE);
        episodes->showSkeleton();
    }

    // What the list already knows is shown right away; the details fill in the rest
    tsvitch::XtreamDetail known;
    known.title  = item.title;
    known.cover  = item.logo;
    known.rating = item.rating;
    known.year   = item.year;
    this->showDetail(known);
    this->updateFavoriteLabel();
    brls::Application::giveFocus(playButton);
    this->loadDetail();
    this->requestTmdb();
}

void XtreamDetailActivity::requestTmdb() {
    auto& tmdb = tsvitch::TmdbService::instance();
    int type = 0, id = Catalog::instance().tmdbOf(item, type);
    if (!tmdb.enabled() || id <= 0) return;
    auto alive = this->alive;
    tmdb.details(type, id, [this, alive, id](const tsvitch::TmdbDetails& result) {
        if (alive->load() && result.ok) this->showTmdb(id, result);
    });
}

namespace {
std::string groupThousands(int number) {
    std::string digits = std::to_string(number), out;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += '.';
        out += digits[i];
    }
    return out;
}
}  // namespace

void XtreamDetailActivity::showTmdb(int id, const tsvitch::TmdbDetails& tmdb) {
    if (tmdb.votes > 0) {
        tmdbVote  = tmdb.vote;
        tmdbVotes = tmdb.votes;
        rating->setText(fmt::format("★ {:.1f}", tmdb.vote) + "   " +
                        brls::getStr("tsvitch/detail/votes", groupThousands(tmdb.votes)));
        ratingBox->setVisibility(brls::Visibility::VISIBLE);
    }
    tmdbOverview = tmdb.overview;
    if (detail.plot.empty() && !tmdbOverview.empty()) plot->setText(tmdbOverview);
    // A series needs the room for its episodes
    if (isSeries) return;

    if (!tmdb.people.empty()) {
        peopleBox->clearViews();
        size_t count = 0;
        for (const auto& person : tmdb.people) {
            if (count++ == 9) break;
            peopleBox->addView(new PersonCard(person));
        }
        peopleShown = true;
        peopleBox->setVisibility(brls::Visibility::VISIBLE);
        director->setVisibility(brls::Visibility::GONE);
        cast->setVisibility(brls::Visibility::GONE);
    }

    // The other movies of its film series first, then what TMDB recommends; only titles of the catalogue
    std::vector<tsvitch::LiveM3u8> similar;
    std::unordered_set<int64_t> seen{TmdbStore::key(1, id)};
    for (const auto& movie : tsvitch::discover::sameCollection(tmdb.collection))
        if (seen.insert(TmdbStore::key(1, movie.tmdb)).second) similar.push_back(movie);
    for (const auto& ref : tmdb.recommendations) {
        if (similar.size() >= 15) break;
        const auto* found = Catalog::instance().find(ref.type, ref.id);
        if (found && seen.insert(TmdbStore::key(ref.type, ref.id)).second) similar.push_back(*found);
    }
    if (similar.size() >= 3) {
        tsvitch::discover::Shelf shelf;
        shelf.id    = "similar";
        shelf.title = "tsvitch/detail/similar"_i18n;
        shelf.items = std::move(similar);
        shelf.notes.assign(shelf.items.size(), "");
        shelf.progress.assign(shelf.items.size(), "");
        auto alive = this->alive;
        similarBox->clearViews();
        similarBox->addView(new DiscoverShelfView(
            std::move(shelf),
            [alive](const tsvitch::LiveM3u8& other) {
                if (alive->load()) Intent::openXtreamDetail(other);
            },
            nullptr, true));
        similarBox->setVisibility(brls::Visibility::VISIBLE);
    }
    // Room for the rows
    if (peopleShown || similarBox->getVisibility() == brls::Visibility::VISIBLE) plot->setMaxRows(3);
}

void XtreamDetailActivity::loadDetail() {
    if (isSeries) episodes->showSkeleton();
    auto alive   = this->alive;
    auto onError = [this, alive](const std::string& error, int) {
        if (!alive->load()) return;
        brls::Logger::error("XtreamDetail: {}", error);
        if (isSeries) episodes->setError("tsvitch/detail/load_error"_i18n);
        // Usually the provider does not answer for a while: the user can ask again
        auto* dialog = new brls::Dialog("tsvitch/detail/load_error_retry"_i18n);
        dialog->addButton("tsvitch/detail/retry"_i18n, [this, alive]() {
            if (alive->load()) this->loadDetail();
        });
        dialog->addButton("hints/cancel"_i18n, []() {});
        dialog->open();
    };
    auto onDetail = [this, alive](tsvitch::XtreamDetail result) {
        if (alive->load()) this->showDetail(result);
    };
    if (isSeries) {
        std::string seriesId = item.url.substr(SERIES_SCHEME.size());
        ProgramConfig::instance().setSettingItem(SettingItem::XTREAM_LAST_SERIES, seriesId);
        CLIENT::get_xtream_series_detail(seriesId, onDetail, onError);
    } else {
        CLIENT::get_xtream_movie_detail(item.id, onDetail, onError);
    }
}

void XtreamDetailActivity::showDetail(const tsvitch::XtreamDetail& update) {
    bool newPoster   = !update.cover.empty() && update.cover != detail.cover;
    bool newBackdrop = !update.backdrop.empty() && update.backdrop != detail.backdrop;
    if (!update.title.empty()) detail.title = update.title;
    if (!update.cover.empty()) detail.cover = update.cover;
    if (!update.backdrop.empty()) detail.backdrop = update.backdrop;
    if (update.rating > 0) detail.rating = update.rating;
    if (update.year > 0) detail.year = update.year;
    const std::pair<std::string*, const std::string*> textFields[] = {
        {&detail.originalTitle, &update.originalTitle}, {&detail.plot, &update.plot},
        {&detail.genre, &update.genre},                 {&detail.cast, &update.cast},
        {&detail.director, &update.director},           {&detail.country, &update.country},
        {&detail.duration, &update.duration}};
    for (const auto& [field, value] : textFields)
        if (!value->empty()) *field = *value;
    bool newSeasons = !update.seasons.empty();
    if (newSeasons) detail.seasons = update.seasons;

    title->setText(detail.title);
    bool showOriginal = !detail.originalTitle.empty() && detail.originalTitle != detail.title;
    subtitle->setText(detail.originalTitle);
    subtitle->setVisibility(showOriginal ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (tmdbVotes > 0) {
        // TMDB's rating with its votes stays
    } else if (detail.rating > 0) {
        rating->setText(fmt::format("★ {:.1f}", detail.rating));
        ratingBox->setVisibility(brls::Visibility::VISIBLE);
    } else {
        ratingBox->setVisibility(brls::Visibility::GONE);
    }
    meta->setText(joinParts({detail.year > 0 ? std::to_string(detail.year) : "", detail.genre,
                             formatDuration(detail.duration), detail.country}));
    plot->setText(detail.plot.empty() ? tmdbOverview : detail.plot);
    director->setText(detail.director.empty() ? "" : brls::getStr("tsvitch/detail/director", detail.director));
    cast->setText(detail.cast.empty() ? "" : brls::getStr("tsvitch/detail/cast", detail.cast));
    if (newPoster) ImageHelper::with(poster)->load(tmdbSize(detail.cover, "w342"));
    if (newBackdrop) ImageHelper::with(backdrop)->load(tmdbSize(detail.backdrop, "w780"));

    this->updatePlayButtons();
    if (isSeries && newSeasons) {
        size_t count = 0;
        for (const auto& season : detail.seasons) count += season.episodes.size();
        facts->setText(brls::getStr("tsvitch/detail/season_count", detail.seasons.size(), count));
        this->buildSeasonButtons();
        bool severalSeasons = detail.seasons.size() > 1;
        this->getContentView()->setActionAvailable(brls::BUTTON_LB, severalSeasons);
        this->getContentView()->setActionAvailable(brls::BUTTON_RB, severalSeasons);
        seasonPrev->setVisibility(severalSeasons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        seasonNext->setVisibility(severalSeasons ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        // Open the season of the episode the play button offers (where the user left off)
        this->showSeason(resumeSeason);
    }
}

void XtreamDetailActivity::showSeason(size_t index, size_t focusEpisode) {
    if (index >= detail.seasons.size()) return;
    currentSeason      = index;
    const auto& season = detail.seasons[index];
    this->updateSeasonButtons();
    auto alive = this->alive;
    // When the focus is in the list it stays there, on the given episode of the new season
    episodes->reloadWithFocus(focusEpisode < season.episodes.size() ? focusEpisode : 0,
                              new EpisodeDataSource(
                                  detail.title, season.episodes,
                                  [this, alive, index](size_t episode) {
                                      if (alive->load()) this->playEpisode(index, episode);
                                  },
                                  // Y on a row marked the episode: the play button follows
                                  [this, alive]() {
                                      if (alive->load()) this->updatePlayButtons();
                                  }));
}

void XtreamDetailActivity::buildSeasonButtons() {
    seasonsBox->clearViews();
    seasonButtons.clear();
    auto alive = this->alive;
    for (size_t i = 0; i < detail.seasons.size(); i++) {
        auto* button = new CustomButton();
        button->setFocusable(true);
        button->setHeight(40);
        button->setPaddingLeft(16);
        button->setPaddingRight(16);
        button->setMarginRight(10);
        button->setCornerRadius(8);
        button->setHighlightCornerRadius(8);
        button->setHideHighlightBackground(true);
        button->setAlignItems(brls::AlignItems::CENTER);
        auto* label = new brls::Label();
        label->setFontSize(17);
        label->setSingleLine(true);
        label->setText(detail.seasons[i].name);
        button->addView(label);
        button->registerClickAction([this, alive, i](brls::View*) {
            if (alive->load()) this->showSeason(i);
            return true;
        });
        button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
        seasonsBox->addView(button);
        seasonButtons.emplace_back(button, label);
    }
}

void XtreamDetailActivity::updateSeasonButtons() {
    static const NVGcolor selected = nvgRGB(255, 145, 0);
    static const NVGcolor other    = nvgRGBA(255, 255, 255, 38);
    for (size_t i = 0; i < seasonButtons.size(); i++) {
        seasonButtons[i].first->setBackgroundColor(i == currentSeason ? selected : other);
        seasonButtons[i].second->setTextColor(nvgRGB(255, 255, 255));
    }
}

void XtreamDetailActivity::updatePlayButtons() {
    if (!isSeries) {
        int64_t position = 0, duration = 0;
        bool started = tsvitch::PlaybackPositionManager::getProgress(item.url, position, duration);
        bool watched = tsvitch::WatchedManager::isWatched(item.url);
        playLabel->setText(started ? brls::getStr("tsvitch/detail/resume", formatPosition(position))
                                   : "tsvitch/detail/play"_i18n);
        restartButton->setVisibility(started ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        watchedChip->setVisibility(watched ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        // How far it was played, on the poster
        float part = started && duration > 0 ? static_cast<float>(position) / duration : watched ? 1.0f : -1.0f;
        if (part >= 0) posterProgress->setProgress(part);
        posterProgress->setVisibility(part >= 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->registerWatchedAction();
        return;
    }

    resumeSeason   = 0;
    resumeEpisode  = 0;
    hasLastWatched = false;
    if (detail.seasons.empty()) {
        playLabel->setText("tsvitch/detail/play"_i18n);
        this->registerWatchedAction();
        return;
    }

    // The most recently watched episode of this series, from the watch history
    std::unordered_map<std::string, std::pair<size_t, size_t>> where;
    for (size_t s = 0; s < detail.seasons.size(); s++)
        for (size_t e = 0; e < detail.seasons[s].episodes.size(); e++)
            where[detail.seasons[s].episodes[e].item.url] = {s, e};
    bool found = false;
    for (const auto& watched : HistoryManager::get()->recent(200)) {
        auto it = where.find(watched.url);
        if (it == where.end()) continue;
        std::tie(resumeSeason, resumeEpisode) = it->second;
        found = hasLastWatched = true;
        lastSeason  = resumeSeason;
        lastEpisode = resumeEpisode;
        break;
    }

    auto urlOf = [this](size_t s, size_t e) -> const std::string& { return detail.seasons[s].episodes[e].item.url; };
    // The episode after (s, e): the next one of the season, or the first one of the next season
    auto advance = [this](size_t& s, size_t& e) {
        if (e + 1 < detail.seasons[s].episodes.size()) {
            e++;
            return true;
        }
        for (size_t n = s + 1; n < detail.seasons.size(); n++) {
            if (detail.seasons[n].episodes.empty()) continue;
            s = n;
            e = 0;
            return true;
        }
        return false;
    };

    std::string key  = "tsvitch/detail/play_episode";
    int64_t position = 0, duration = 0;
    if (found && tsvitch::PlaybackPositionManager::getProgress(urlOf(resumeSeason, resumeEpisode), position, duration)) {
        // Left in the middle
        key = "tsvitch/detail/resume_episode";
    } else if (!found || tsvitch::WatchedManager::isWatched(urlOf(resumeSeason, resumeEpisode))) {
        // Watched: the first episode after it that is not watched yet (after the last episode of a season, the
        // next season's first one). Nothing watched yet: the first episode not marked as watched.
        size_t s = resumeSeason, e = resumeEpisode;
        bool ok  = found ? advance(s, e) : !detail.seasons[s].episodes.empty() || advance(s, e);
        while (ok && tsvitch::WatchedManager::isWatched(urlOf(s, e))) ok = advance(s, e);
        if (ok) {
            resumeSeason  = s;
            resumeEpisode = e;
            if (found) key = "tsvitch/detail/next_episode";
        }
        // Everything after it is watched: the episode watched last is offered again
    }
    if (resumeEpisode >= detail.seasons[resumeSeason].episodes.size()) {
        playLabel->setText("tsvitch/detail/play"_i18n);
        this->registerWatchedAction();
        return;
    }
    const auto& season  = detail.seasons[resumeSeason];
    const auto& episode = season.episodes[resumeEpisode];
    int seasonNumber    = season.number > 0 ? season.number : static_cast<int>(resumeSeason) + 1;
    int episodeNumber   = episode.number > 0 ? episode.number : static_cast<int>(resumeEpisode) + 1;
    playLabel->setText(brls::getStr(key, seasonNumber, episodeNumber));
    this->registerWatchedAction();
}

std::string XtreamDetailActivity::watchedTarget() const {
    if (!isSeries) return item.url;
    if (resumeSeason < detail.seasons.size() && resumeEpisode < detail.seasons[resumeSeason].episodes.size())
        return detail.seasons[resumeSeason].episodes[resumeEpisode].item.url;
    return "";
}

void XtreamDetailActivity::registerWatchedAction() {
    std::string target = this->watchedTarget();
    if (target.empty()) return;
    bool watched = tsvitch::WatchedManager::isWatched(target);
    this->getContentView()->registerAction(
        watched ? "tsvitch/detail/mark_unwatched"_i18n : "tsvitch/detail/mark_watched"_i18n, brls::BUTTON_Y,
        [this](brls::View*) {
            std::string url = this->watchedTarget();
            if (url.empty()) return true;
            tsvitch::WatchedManager::setWatched(url, !tsvitch::WatchedManager::isWatched(url));
            size_t season = currentSeason;
            this->updatePlayButtons();
            // A series: the row of that episode shows its new state
            if (isSeries) this->showSeason(season);
            brls::Application::getGlobalHintsUpdateEvent()->fire();
            return true;
        });
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void XtreamDetailActivity::play(bool fromStart) {
    if (isSeries) {
        if (!detail.seasons.empty()) this->playEpisode(resumeSeason, resumeEpisode);
        return;
    }
    if (fromStart) tsvitch::PlaybackPositionManager::clearPosition(item.url);
    if (!ProgramConfig::instance().isAdultCategory(item.groupTitle)) HistoryManager::get()->add(item);
    auto alive = this->alive;
    Intent::openLive({item}, 0, [this, alive]() {
        if (alive->load()) this->updatePlayButtons();
    });
}

void XtreamDetailActivity::playEpisode(size_t seasonIndex, size_t episodeIndex) {
    if (seasonIndex >= detail.seasons.size()) return;
    if (episodeIndex >= detail.seasons[seasonIndex].episodes.size()) return;

    // Every episode of every season goes to the player in order: next/previous and the automatic next
    // episode continue into the following season
    std::vector<tsvitch::LiveM3u8> playlist;
    size_t start = 0;
    for (size_t s = 0; s < detail.seasons.size(); s++) {
        if (s == seasonIndex) start = playlist.size() + episodeIndex;
        for (const auto& episode : detail.seasons[s].episodes) {
            playlist.push_back(episode.item);
            // The history keeps the series of an episode: the discovery screen continues it from its page
            playlist.back().seriesId = item.id;
        }
    }
    if (!ProgramConfig::instance().isAdultCategory(item.groupTitle)) HistoryManager::get()->add(playlist[start]);

    auto alive = this->alive;
    Intent::openLive(
        playlist, start,
        [this, alive]() {
            if (!alive->load()) return;
            // The focus came back to a row of the old list; the list is rebuilt (new resume badges), so the
            // episode watched last is shown and, if the focus was in the list, focused again
            this->updatePlayButtons();
            this->showSeason(hasLastWatched ? lastSeason : resumeSeason, hasLastWatched ? lastEpisode : 0);
        },
        true);
}

void XtreamDetailActivity::toggleFavorite() {
    FavoriteManager::get()->toggle(item);
    this->updateFavoriteLabel();
}

void XtreamDetailActivity::updateFavoriteLabel() {
    favoriteLabel->setText(FavoriteManager::get()->isFavorite(item.url) ? "tsvitch/detail/favorite_remove"_i18n
                                                                        : "tsvitch/detail/favorite_add"_i18n);
}
