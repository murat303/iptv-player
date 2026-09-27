

#pragma once

#include "view/recycling_grid.hpp"
#include "api/tsvitch/result/home_live_result.h" 

class SVGImage;
class TextBox;
class ProgressLine;

class BaseVideoCard : public RecyclingGridItem {
public:
    void prepareForReuse() override;

    void cacheForReuse() override;

protected:
    BRLS_BIND(brls::Image, picture, "video/card/picture");
};

class RecyclingGridItemLiveVideoCard : public BaseVideoCard {
public:
    // posterLayout: big 2:3 poster with title below (movies and series) instead of the channel card
    explicit RecyclingGridItemLiveVideoCard(bool posterLayout = false);

    ~RecyclingGridItemLiveVideoCard() override;

    // showGroup: the category tag under a channel (hidden when the list is one category already)
    // loadPicture: false leaves the picture for loadPicture() (shelves load it once they come near the screen)
    void setChannel(tsvitch::LiveM3u8 liveData, bool showGroup = true, bool loadPicture = true);

    void loadPicture();

    // Frees the picture of a card far from the screen (loadPicture shows it again)
    void releasePicture();

    // Shelves: a smaller poster than in the grid
    void setPosterHeight(float height);

    // A line instead of the year ("S2 · E4" on the discovery screen)
    void setNote(const std::string& note);

    // The bar and the check show this video's state (a series card shows its episode)
    void setWatchUrl(const std::string& url);

   tsvitch::LiveM3u8 getChannel();

                                             void setFavoriteIcon(bool isFavorite);

    static RecyclingGridItemLiveVideoCard* create();

    static RecyclingGridItemLiveVideoCard* createPoster();

    // Movies and episodes: a check once watched, and a bar under the picture with how far they were played
    void showWatchState();

    // After a detail screen marked something: the cards on the screen show it without a reload
    static void refreshWatchStates(RecyclingGrid* grid);

private:
tsvitch::LiveM3u8 liveData;
    bool posterLayout = false;
    bool pictureLoaded = false;
    std::string watchUrl;
    BRLS_BIND(TextBox, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelGroup, "video/card/label/group");
    BRLS_BIND(brls::Label, labelChno, "video/card/label/chno");
    BRLS_BIND(brls::Box, boxPic, "video/card/pic_box");
    BRLS_BIND(brls::Box, boxHint, "video/card/hint");
    BRLS_BIND(SVGImage, svgUp, "video/svg/up");
    BRLS_BIND(SVGImage, svgFavoriteIcon, "video/card/ico/favorite");
    BRLS_BIND(SVGImage, svgWatched, "video/card/ico/watched");
    BRLS_BIND(ProgressLine, progressLine, "video/card/progress");
};