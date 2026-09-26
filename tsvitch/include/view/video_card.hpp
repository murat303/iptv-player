

#pragma once

#include "view/recycling_grid.hpp"
#include "api/tsvitch/result/home_live_result.h" 

class SVGImage;
class TextBox;

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
    void setChannel(tsvitch::LiveM3u8 liveData, bool showGroup = true);

   tsvitch::LiveM3u8 getChannel();

                                             void setFavoriteIcon(bool isFavorite);

    static RecyclingGridItemLiveVideoCard* create();

    static RecyclingGridItemLiveVideoCard* createPoster();

private:
tsvitch::LiveM3u8 liveData;
    bool posterLayout = false;
    BRLS_BIND(TextBox, labelTitle, "video/card/label/title");
    BRLS_BIND(brls::Label, labelGroup, "video/card/label/group");
    BRLS_BIND(brls::Label, labelChno, "video/card/label/chno");
    BRLS_BIND(brls::Box, boxPic, "video/card/pic_box");
    BRLS_BIND(brls::Box, boxHint, "video/card/hint");
    BRLS_BIND(SVGImage, svgUp, "video/svg/up");
    BRLS_BIND(SVGImage, svgFavoriteIcon, "video/card/ico/favorite");
};