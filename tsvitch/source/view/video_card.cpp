

#include "view/video_card.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "utils/number_helper.hpp"
#include "utils/image_helper.hpp"
#include "core/FavoriteManager.hpp"
#include <pystring.h>

using namespace brls::literals;

void BaseVideoCard::prepareForReuse() { this->picture->setImageFromRes("pictures/video-card-bg.png"); }

void BaseVideoCard::cacheForReuse() { ImageHelper::clear(this->picture); }

RecyclingGridItemLiveVideoCard::RecyclingGridItemLiveVideoCard(bool posterLayout) : posterLayout(posterLayout) {
    this->inflateFromXMLRes(posterLayout ? "xml/views/video_card_poster.xml" : "xml/views/video_card_live.xml");
}

RecyclingGridItemLiveVideoCard::~RecyclingGridItemLiveVideoCard() { ImageHelper::clear(this->picture); }

void RecyclingGridItemLiveVideoCard::setChannel(tsvitch::LiveM3u8 liveData, bool showGroup) {
    this->liveData = liveData;
    this->labelTitle->setIsWrapping(posterLayout);
    this->labelTitle->setText(liveData.title);
    ImageHelper::with(this->picture)->load(ImageHelper::smallPoster(liveData.logo));

    // Movies and series: the category is already chosen on the left and the server's running number
    // means nothing, so the card shows the rating instead
    if (liveData.type == 1 || liveData.type == 2) {
        this->labelChno->setText(liveData.year > 0 ? std::to_string(liveData.year) : "");
        if (liveData.rating > 0) {
            this->labelGroup->setText(fmt::format("★ {:.1f}", liveData.rating));
            this->boxHint->setVisibility(brls::Visibility::VISIBLE);
        } else {
            this->boxHint->setVisibility(brls::Visibility::GONE);
        }
    } else {
        this->labelChno->setText(liveData.chno);
        this->labelGroup->setText(liveData.groupTitle);
        this->boxHint->setVisibility(showGroup ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    bool isFavorite = FavoriteManager::get()->isFavorite(liveData.url);

    if (isFavorite) {
        this->svgFavoriteIcon->setImageFromSVGRes("svg/ico-favorites-activate.svg");
        this->svgFavoriteIcon->setVisibility(brls::Visibility::VISIBLE);
    } else
        this->svgFavoriteIcon->setVisibility(brls::Visibility::GONE);
}

tsvitch::LiveM3u8 RecyclingGridItemLiveVideoCard::getChannel() { return this->liveData; }

void RecyclingGridItemLiveVideoCard::setFavoriteIcon(bool isFavorite) {
    if (isFavorite) {
        this->svgFavoriteIcon->setImageFromSVGRes("svg/ico-favorites-activate.svg");
        this->svgFavoriteIcon->setVisibility(brls::Visibility::VISIBLE);
    } else
        this->svgFavoriteIcon->setVisibility(brls::Visibility::GONE);
}

RecyclingGridItemLiveVideoCard* RecyclingGridItemLiveVideoCard::create() {
    return new RecyclingGridItemLiveVideoCard();
}

RecyclingGridItemLiveVideoCard* RecyclingGridItemLiveVideoCard::createPoster() {
    return new RecyclingGridItemLiveVideoCard(true);
}
