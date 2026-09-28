#pragma once

#include <vector>

#include "api/tsvitch/result/home_live_result.h"
#include "view/auto_tab_frame.hpp"

class RecyclingGrid;
class CustomButton;
class FavoriteKindChips;
namespace brls {
class Label;
}

/// The favorites tab: live channels, movies and series, one kind at a time (a chip per kind when there are several,
/// L and R change it). Channels show as channel cards, movies and series as posters like in the lists.
class HomeFavorites : public AttachedView {
public:
    HomeFavorites();

    ~HomeFavorites();

    static View *create();

    void onCreate() override;

    void onShow() override;

    // From the sidebar the focus goes to the cards (the chips are above them)
    View *getDefaultFocus() override;

    void onError(const std::string &error);

    void refreshFavorites();

    void toggleFavorite();

    void downloadVideo();

private:
    struct KindChip {
        CustomButton *button;
        brls::Label *label;
        int kind;
    };

    // Reads the favorites again and shows the kind shown last (or the first one left); focus: the card focused when
    // the focus is in the grid
    void reload(size_t focus);

    // The favorites of one kind (0 live, 1 movies, 2 series) in the grid
    void showKind(int kind, size_t focus = 0);

    // L and R: the next or previous kind
    void stepKind(int step);

    void buildKindChips();

    void updateKindChips();

    // The focus is on a card of the grid (not on a chip or the sidebar)
    bool focusInGrid();

    std::vector<tsvitch::LiveM3u8> favoritesOf(int kind) const;

    tsvitch::LiveM3u8ListResult favoritesList;
    std::vector<int> kinds;  // the kinds that have favorites, in the order of their chips
    std::vector<KindChip> kindChips;
    FavoriteKindChips *chipRow = nullptr;
    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/favorites/recyclingGrid");
    BRLS_BIND(brls::Box, kindBox, "home/favorites/kinds");
    BRLS_BIND(brls::Label, kindPrev, "home/favorites/kinds/prev");
    BRLS_BIND(brls::Label, kindNext, "home/favorites/kinds/next");
};
