#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <borealis/core/event.hpp>

#include "api/tmdb.hpp"
#include "api/tsvitch/result/home_live_result.h"

/// What the discovery screen shows: shelves of titles and of collections, built from the catalogue (the saved
/// movie and series lists), the TMDB data of its titles, the history and a few TMDB lists. Everything runs on the
/// UI thread and only reads what is in memory.
namespace tsvitch::discover {

/// A page of titles: a genre, a theme, a studio, an award, a decade, a film series
struct Collection {
    std::string id;     // "genre:1:action", "theme:books", "award:palme_dor", "franchise:1241", ...
    std::string title;
    int type        = 0;  // 1 movies, 2 series, 0 both
    uint32_t colorA = 0;  // cover gradient, 0xRRGGBB
    uint32_t colorB = 0;
    size_t count    = 0;  // titles in it (items may hold only the first ones)
    std::vector<LiveM3u8> items;
};

struct Shelf {
    enum Kind { POSTERS, GENRES, COVERS };
    std::string id;  // stable id, also for the setting that hides shelves
    std::string title;
    Kind kind = POSTERS;
    std::vector<LiveM3u8> items;          // POSTERS
    std::vector<std::string> notes;       // POSTERS: a line under the title instead of the year ("S2 · E4")
    std::vector<std::string> progress;    // POSTERS: the url whose playback position the card shows ("" = its own)
    std::vector<Collection> collections;  // GENRES, COVERS
};

struct Hero {
    bool valid = false;
    LiveM3u8 item;
    std::string backdrop;  // full url
    std::string label;     // "Trending this week"
    std::string meta;      // "★ 7.9 · 2024 · Action, Thriller"
};

/// A watched or favourite title and what TMDB recommends after it
struct Seed {
    LiveM3u8 item;  // the movie, or the series of an episode
    int type = 1;
    int tmdb = 0;
    std::vector<TmdbRef> recommendations;
};

struct Inputs {
    std::vector<TmdbRef> trending;  // this week's movies and series, in TMDB's order
    std::vector<TmdbRef> popularMovies, popularSeries;  // TMDB's popular lists of the day
    std::vector<Seed> seeds;
};

struct Page {
    Hero hero;
    std::vector<Shelf> shelves;
};

/// Titles from the history and the favourites, newest first, that have a TMDB id (at most max)
std::vector<Seed> seeds(size_t max);

/// Every shelf: its id and the i18n key of its name (the settings list them)
const std::vector<std::pair<std::string, std::string>>& shelfList();
bool isHidden(const std::string& shelfId);
void setHidden(const std::string& shelfId, bool hidden);

/// Fires when the settings changed what the page shows (hidden shelves, TMDB on or off)
brls::Event<>* getChangedEvent();

Page build(const Inputs& inputs);

/// The genres of movies (1) or series (2) that have titles, the most common first (collections without items)
std::vector<Collection> genreTiles(int type);

/// A whole collection by its id, every title in it (for its page); empty items when it is unknown
Collection collection(const std::string& id);

/// Poster url of a title for a card (the provider's picture, TMDB's in the small size)
std::string posterOf(const LiveM3u8& item);

/// A title without the provider's tags at its end: "Pati (2023) TR" -> "Pati", "Film - 2014 4K HDR" -> "Film"
std::string cleanTitle(const std::string& title);

}  // namespace tsvitch::discover
