#include "activity/xtream_detail_activity.hpp"

#include <cctype>
#include <cstdio>
#include <tuple>
#include <unordered_map>
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
#include "view/custom_button.hpp"
#include "view/grid_dropdown.hpp"
#include "view/recycling_grid.hpp"
#include "view/text_box.hpp"
#include "utils/text_fold.hpp"
#include "utils/video_download.hpp"

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

    void setEpisode(const std::string& seriesTitle, const tsvitch::XtreamEpisode& episode, int64_t position) {
        this->item = episode.item;
        title->setText(episodeTitle(seriesTitle, episode));
        std::string rating = episode.item.rating > 0 ? fmt::format("★ {:.1f}", episode.item.rating) : "";
        meta->setText(joinParts({formatDuration(episode.duration), rating, episode.airDate}));
        plot->setText(episode.plot);
        ImageHelper::with(still)->load(episode.item.logo);
        if (position > 0) {
            resumeLabel->setText(formatPosition(position));
            resume->setVisibility(brls::Visibility::VISIBLE);
        } else {
            resume->setVisibility(brls::Visibility::GONE);
        }
    }

    void prepareForReuse() override { still->setImageFromRes("pictures/video-card-bg.png"); }

    void cacheForReuse() override { ImageHelper::clear(still); }

    static RecyclingGridItem* create() { return new EpisodeCell(); }

    const tsvitch::LiveM3u8& getItem() const { return item; }

private:
    tsvitch::LiveM3u8 item;
    BRLS_BIND(brls::Image, still, "episode/still");
    BRLS_BIND(brls::Box, resume, "episode/resume");
    BRLS_BIND(brls::Label, resumeLabel, "episode/resume/label");
    BRLS_BIND(brls::Label, title, "episode/title");
    BRLS_BIND(brls::Label, meta, "episode/meta");
    BRLS_BIND(TextBox, plot, "episode/plot");
};

class EpisodeDataSource : public RecyclingGridDataSource {
public:
    EpisodeDataSource(std::string seriesTitle, std::vector<tsvitch::XtreamEpisode> episodes,
                      std::unordered_map<std::string, int64_t> positions, std::function<void(size_t)> onSelect)
        : seriesTitle(std::move(seriesTitle)),
          episodes(std::move(episodes)),
          positions(std::move(positions)),
          onSelect(std::move(onSelect)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell           = (EpisodeCell*)recycler->dequeueReusableCell("Cell");
        const auto& episode  = episodes[index];
        auto position        = positions.find(episode.item.url);
        cell->setEpisode(seriesTitle, episode, position != positions.end() ? position->second : 0);
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
    std::unordered_map<std::string, int64_t> positions;
    std::function<void(size_t)> onSelect;
};

XtreamDetailActivity::XtreamDetailActivity(const tsvitch::LiveM3u8& item) : item(item) {
    isSeries = item.url.rfind(SERIES_SCHEME, 0) == 0;
}

XtreamDetailActivity::~XtreamDetailActivity() {
    alive->store(false);
    ImageHelper::clear(poster);
    ImageHelper::clear(backdrop);
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
        this->getContentView()->registerAction("tsvitch/detail/previous_season"_i18n, brls::BUTTON_LB,
                                               changeSeason(-1, brls::FocusDirection::LEFT));
        this->getContentView()->registerAction("tsvitch/detail/next_season"_i18n, brls::BUTTON_RB,
                                               changeSeason(1, brls::FocusDirection::RIGHT));
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
    if (isSeries)
        CLIENT::get_xtream_series_detail(item.url.substr(SERIES_SCHEME.size()), onDetail, onError);
    else
        CLIENT::get_xtream_movie_detail(item.id, onDetail, onError);
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
    if (detail.rating > 0) {
        rating->setText(fmt::format("★ {:.1f}", detail.rating));
        ratingBox->setVisibility(brls::Visibility::VISIBLE);
    } else {
        ratingBox->setVisibility(brls::Visibility::GONE);
    }
    meta->setText(joinParts({detail.year > 0 ? std::to_string(detail.year) : "", detail.genre,
                             formatDuration(detail.duration), detail.country}));
    plot->setText(detail.plot);
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
                              new EpisodeDataSource(detail.title, season.episodes,
                                                    tsvitch::PlaybackPositionManager::getAllPositions(),
                                                    [this, alive, index](size_t episode) {
                                                        if (alive->load()) this->playEpisode(index, episode);
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
        int64_t position = tsvitch::PlaybackPositionManager::getPosition(item.url);
        playLabel->setText(position > 0 ? brls::getStr("tsvitch/detail/resume", formatPosition(position))
                                        : "tsvitch/detail/play"_i18n);
        restartButton->setVisibility(position > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        return;
    }

    resumeSeason   = 0;
    resumeEpisode  = 0;
    hasLastWatched = false;
    if (detail.seasons.empty()) {
        playLabel->setText("tsvitch/detail/play"_i18n);
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

    std::string key = "tsvitch/detail/play_episode";
    if (found) {
        const auto& url = detail.seasons[resumeSeason].episodes[resumeEpisode].item.url;
        if (tsvitch::PlaybackPositionManager::getPosition(url) > 0) {
            key = "tsvitch/detail/resume_episode";
        } else {
            // Watched to the end: offer the next one (the last episode of the series is offered again)
            if (resumeEpisode + 1 < detail.seasons[resumeSeason].episodes.size()) {
                resumeEpisode++;
                key = "tsvitch/detail/next_episode";
            } else if (resumeSeason + 1 < detail.seasons.size()) {
                resumeSeason++;
                resumeEpisode = 0;
                key = "tsvitch/detail/next_episode";
            }
        }
    }
    const auto& season  = detail.seasons[resumeSeason];
    const auto& episode = season.episodes[resumeEpisode];
    int seasonNumber    = season.number > 0 ? season.number : static_cast<int>(resumeSeason) + 1;
    int episodeNumber   = episode.number > 0 ? episode.number : static_cast<int>(resumeEpisode) + 1;
    playLabel->setText(brls::getStr(key, seasonNumber, episodeNumber));
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
        for (const auto& episode : detail.seasons[s].episodes) playlist.push_back(episode.item);
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
