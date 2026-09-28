

#include "view/video_card.hpp"
#include "view/svg_image.hpp"
#include "view/text_box.hpp"
#include "utils/number_helper.hpp"
#include "utils/image_helper.hpp"
#include "core/FavoriteManager.hpp"
#include "utils/watched_manager.hpp"
#include "view/progress_line.hpp"
#include <pystring.h>

using namespace brls::literals;

void BaseVideoCard::prepareForReuse() { this->picture->setImageFromRes("pictures/video-card-bg.png"); }

void BaseVideoCard::cacheForReuse() { ImageHelper::clear(this->picture); }

RecyclingGridItemLiveVideoCard::RecyclingGridItemLiveVideoCard(bool posterLayout) : posterLayout(posterLayout) {
    this->inflateFromXMLRes(posterLayout ? "xml/views/video_card_poster.xml" : "xml/views/video_card_live.xml");
}

RecyclingGridItemLiveVideoCard::~RecyclingGridItemLiveVideoCard() { ImageHelper::clear(this->picture); }

void RecyclingGridItemLiveVideoCard::setChannel(tsvitch::LiveM3u8 liveData, bool showGroup, bool loadPicture) {
    this->liveData = liveData;
    this->labelTitle->setIsWrapping(posterLayout);
    this->labelTitle->setText(liveData.title);
    this->pictureLoaded = false;
    if (loadPicture) this->loadPicture();

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
        // No running number on channels either: it is the server's order, not a channel number people use
        this->labelChno->setText("");
        this->labelGroup->setText(liveData.groupTitle);
        this->boxHint->setVisibility(showGroup ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    bool isFavorite = FavoriteManager::get()->isFavorite(liveData);

    if (isFavorite) {
        this->svgFavoriteIcon->setImageFromSVGRes("svg/ico-favorites-activate.svg");
        this->svgFavoriteIcon->setVisibility(brls::Visibility::VISIBLE);
    } else
        this->svgFavoriteIcon->setVisibility(brls::Visibility::GONE);
    this->showWatchState();
}

void RecyclingGridItemLiveVideoCard::loadPicture() {
    if (pictureLoaded) return;
    pictureLoaded = true;
    ImageHelper::with(this->picture)->load(ImageHelper::smallPoster(liveData.logo));
}

void RecyclingGridItemLiveVideoCard::releasePicture() {
    if (!pictureLoaded) return;
    pictureLoaded = false;
    ImageHelper::clear(this->picture);
}

void RecyclingGridItemLiveVideoCard::setPosterHeight(float height) { this->boxPic->setHeight(height); }

void RecyclingGridItemLiveVideoCard::showWatchState() {
    // Channels and series cards have neither (a series card stands for all of its episodes)
    bool video       = liveData.type != 0;
    bool watched     = video && tsvitch::WatchedManager::isWatched(liveData.url);
    int64_t position = 0, duration = 0;
    bool started = video && tsvitch::PlaybackPositionManager::getProgress(liveData.url, position, duration) &&
                   duration > 0;
    float part = started ? static_cast<float>(position) / duration : watched ? 1.0f : -1.0f;
    svgWatched->setVisibility(watched ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (part >= 0) progressLine->setProgress(part);
    progressLine->setVisibility(part >= 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void RecyclingGridItemLiveVideoCard::refreshWatchStates(RecyclingGrid* grid) {
    if (!grid) return;
    for (auto* cell : grid->getGridItems())
        if (auto* card = dynamic_cast<RecyclingGridItemLiveVideoCard*>(cell)) card->showWatchState();
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
