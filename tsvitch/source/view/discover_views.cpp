#include "view/discover_views.hpp"

#include <algorithm>

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>

#include "utils/image_helper.hpp"
#include "view/video_card.hpp"

using namespace brls::literals;

namespace {

NVGcolor rgb(uint32_t color, float alpha = 1.0f) {
    return nvgRGBAf(((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, alpha);
}

NVGcolor withAlpha(NVGcolor color, float alpha) {
    color.a = alpha;
    return color;
}

void fillRounded(NVGcontext* vg, float x, float y, float width, float height, float radius, NVGpaint paint) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, radius);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

// A shelf loads its pictures within this distance of the screen, and frees them beyond three times as much
constexpr float NEAR_DISTANCE = 720;

bool onScreen(float y, float height, float distance) {
    float screen = brls::Application::contentHeight;
    return y < screen + distance && y + height > -distance;
}

/// Two soft rings in the top right corner of a tile give the flat colour some depth. They are gradients painted
/// into the tile's own rounded shape, so nothing crosses its corners.
void paintRings(NVGcontext* vg, float x, float y, float width, float height, float alpha) {
    auto ring = [&](float radius, float edge, unsigned char strength) {
        NVGpaint paint = nvgRadialGradient(vg, x + width - 18, y + 14, radius, radius + edge,
                                           nvgRGBA(255, 255, 255, static_cast<unsigned char>(strength * alpha)),
                                           nvgRGBA(255, 255, 255, 0));
        fillRounded(vg, x, y, width, height, 12, paint);
    };
    ring(50, 2, 30);
    ring(28, 2, 26);
}

brls::Label* makeLabel(float size, NVGcolor color, const std::string& text) {
    auto* label = new brls::Label();
    label->setFontSize(size);
    label->setTextColor(color);
    label->setText(text);
    return label;
}

}  // namespace

// PictureSet

PictureSet::PictureSet(std::vector<std::string> urls) : urls(std::move(urls)) {}

PictureSet::~PictureSet() {
    this->release();
    for (auto* image : images) {
        // An image whose request still runs is deleted once the request lets it go
        if (image->isPtrLocked())
            image->freeView();
        else
            delete image;
    }
}

void PictureSet::load() {
    if (loaded) return;
    loaded = true;
    if (images.empty())
        for (size_t i = 0; i < urls.size(); i++) images.push_back(new brls::Image());
    for (size_t i = 0; i < urls.size(); i++)
        if (!urls[i].empty()) ImageHelper::with(images[i])->load(urls[i]);
}

void PictureSet::release() {
    if (!loaded) return;
    loaded = false;
    for (auto* image : images) ImageHelper::clear(image);
}

bool PictureSet::draw(NVGcontext* vg, size_t i, float x, float y, float width, float height, float radius,
                      float alpha) {
    return this->draw(vg, i, x, y, width, height, radius, radius, radius, radius, alpha);
}

bool PictureSet::draw(NVGcontext* vg, size_t i, float x, float y, float width, float height, float topLeft,
                      float topRight, float bottomRight, float bottomLeft, float alpha) {
    if (i >= images.size()) return false;
    int texture = images[i]->getTexture();
    float iw = images[i]->getOriginalImageWidth(), ih = images[i]->getOriginalImageHeight();
    if (texture <= 0 || iw <= 2 || ih <= 2) return false;
    // ImageHelper adds a transparent pixel around every picture
    iw -= 2;
    ih -= 2;
    float scale = std::max(width / iw, height / ih);
    float dw = iw * scale, dh = ih * scale;
    float dx = x + (width - dw) / 2, dy = y + (height - dh) / 2;
    // The pattern covers the padded picture: one scaled pixel outside on every side
    NVGpaint paint = nvgImagePattern(vg, dx - scale, dy - scale, dw + 2 * scale, dh + 2 * scale, 0, texture, alpha);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, width, height, topLeft, topRight, bottomRight, bottomLeft);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    return true;
}

// DiscoverHero

DiscoverHero::DiscoverHero(const tsvitch::discover::Hero& hero, std::function<void(const tsvitch::LiveM3u8&)> open)
    : item(hero.item), backdrop({hero.backdrop}) {
    this->setFocusable(true);
    this->setHeight(330);
    this->setMarginTop(16);
    this->setMarginLeft(30);
    this->setMarginRight(30);
    this->setMarginBottom(8);
    this->setCornerRadius(16);
    this->setHighlightCornerRadius(18);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setPadding(0, 40, 34, 44);

    auto* chip = makeLabel(15, nvgRGB(255, 150, 40), hero.label);
    auto* title = makeLabel(36, nvgRGB(255, 255, 255), tsvitch::discover::cleanTitle(hero.item.title));
    title->setMarginTop(6);
    title->setSingleLine(true);
    title->setWidth(640);
    auto* meta = makeLabel(17, nvgRGBA(255, 255, 255, 215), hero.meta);
    meta->setMarginTop(8);
    auto* hint = makeLabel(16, nvgRGBA(255, 255, 255, 190), "  " + "tsvitch/discover/details"_i18n);
    hint->setMarginTop(16);
    this->addView(chip);
    this->addView(title);
    this->addView(meta);
    this->addView(hint);

    auto target = this->item;
    this->registerClickAction([target, open](brls::View*) {
        if (open) open(target);
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

DiscoverHero::~DiscoverHero() { *alive = false; }

void DiscoverHero::setVisibleOnScreen(bool visible) {
    if (visible)
        backdrop.load();
    else
        backdrop.release();
}

void DiscoverHero::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                        brls::FrameContext* ctx) {
    bool near = onScreen(y, height, backdrop.isLoaded() ? NEAR_DISTANCE * 3 : NEAR_DISTANCE);
    if (near != backdrop.isLoaded() && !pendingChange) {
        // Loading or freeing pictures changes nothing in the view tree, but it waits for the next frame anyway
        pendingChange = true;
        auto isAlive  = alive;
        brls::sync([this, near, isAlive]() {
            if (!*isAlive) return;
            pendingChange = false;
            this->setVisibleOnScreen(near);
        });
    }
    NVGcolor background = brls::Application::getTheme().getColor("brls/background");
    fillRounded(vg, x, y, width, height, 16,
                nvgLinearGradient(vg, x, y, x + width, y + height, a(nvgRGB(38, 40, 48)), a(nvgRGB(18, 19, 24))));
    backdrop.draw(vg, 0, x, y, width, height, 16, this->alpha);
    // The text stays readable over any picture: a shade from the left and one from the bottom
    fillRounded(vg, x, y, width, height, 16,
                nvgLinearGradient(vg, x, y, x + width * 0.7f, y, a(withAlpha(background, 0.94f)),
                                  a(withAlpha(background, 0.0f))));
    fillRounded(vg, x, y, width, height, 16,
                nvgLinearGradient(vg, x, y + height * 0.45f, x, y + height, a(withAlpha(background, 0.0f)),
                                  a(withAlpha(background, 0.75f))));
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

// GenreTile

GenreTile::GenreTile(const tsvitch::discover::Collection& genre, std::function<void()> onClick)
    : colorA(rgb(genre.colorA)), colorB(rgb(genre.colorB)) {
    this->setFocusable(true);
    this->setWidth(WIDTH);
    this->setHeight(HEIGHT);
    this->setMarginRight(16);
    this->setCornerRadius(12);
    this->setHighlightCornerRadius(14);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setPadding(0, 14, 14, 16);
    auto* name = makeLabel(23, nvgRGB(255, 255, 255), genre.title);
    name->setSingleLine(true);
    auto* count = makeLabel(14, nvgRGBA(255, 255, 255, 210), brls::getStr("tsvitch/discover/count", genre.count));
    count->setMarginTop(2);
    this->addView(name);
    this->addView(count);
    this->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void GenreTile::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                     brls::FrameContext* ctx) {
    fillRounded(vg, x, y, width, height, 12, nvgLinearGradient(vg, x, y, x + width, y + height, a(colorA), a(colorB)));
    paintRings(vg, x, y, width, height, this->alpha);
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

// GenreGridCell

GenreGridCell::GenreGridCell() : colorA(nvgRGB(69, 90, 100)), colorB(nvgRGB(38, 50, 56)) {
    this->setHeight(118);
    this->setCornerRadius(12);
    this->setHighlightCornerRadius(14);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setPadding(0, 14, 14, 16);
    name = makeLabel(23, nvgRGB(255, 255, 255), "");
    name->setSingleLine(true);
    count = makeLabel(14, nvgRGBA(255, 255, 255, 210), "");
    count->setMarginTop(2);
    this->addView(name);
    this->addView(count);
}

void GenreGridCell::setGenre(const tsvitch::discover::Collection& genre) {
    colorA = rgb(genre.colorA);
    colorB = rgb(genre.colorB);
    name->setText(genre.title);
    count->setText(brls::getStr("tsvitch/discover/count", genre.count));
}

void GenreGridCell::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx) {
    fillRounded(vg, x, y, width, height, 12, nvgLinearGradient(vg, x, y, x + width, y + height, a(colorA), a(colorB)));
    paintRings(vg, x, y, width, height, this->alpha);
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

RecyclingGridItem* GenreGridCell::create() { return new GenreGridCell(); }

// CoverCard

namespace {
std::vector<std::string> coverUrls(const tsvitch::discover::Collection& collection) {
    std::vector<std::string> urls;
    for (const auto& item : collection.items) {
        if (urls.size() == 3) break;
        if (!item.logo.empty()) urls.push_back(tsvitch::discover::posterOf(item));
    }
    return urls;
}
}  // namespace

CoverCard::CoverCard(const tsvitch::discover::Collection& collection, std::function<void()> onClick)
    : colorA(rgb(collection.colorA)), colorB(rgb(collection.colorB)), posters(coverUrls(collection)) {
    this->setFocusable(true);
    this->setWidth(WIDTH);
    this->setHeight(HEIGHT);
    this->setMarginRight(18);
    this->setCornerRadius(12);
    this->setHighlightCornerRadius(14);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_END);
    this->setPadding(0, 16, 14, 18);
    // Long names take two lines
    auto* name = makeLabel(21, nvgRGB(255, 255, 255), collection.title);
    name->setWidth(WIDTH - 36);
    name->setLineHeight(1.1f);
    auto* count = makeLabel(14, nvgRGBA(255, 255, 255, 215), brls::getStr("tsvitch/discover/count", collection.count));
    count->setMarginTop(2);
    this->addView(name);
    this->addView(count);
    this->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();
        return true;
    });
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void CoverCard::setVisibleOnScreen(bool visible) {
    if (visible)
        posters.load();
    else
        posters.release();
}

void CoverCard::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                     brls::FrameContext* ctx) {
    fillRounded(vg, x, y, width, height, 12, nvgLinearGradient(vg, x, y, x + width, y + height, a(colorA), a(colorB)));
    // Three posters side by side over the colour, the first on the right (the text sits on the left)
    size_t count = posters.size();
    float column = width / 3.0f;
    for (size_t i = 0; i < count; i++) {
        float px = x + width - column * static_cast<float>(i + 1);
        // The rightmost poster rounds its right corners like the card, the leftmost (the third) its left ones
        float right = i == 0 ? 12.0f : 0.0f;
        float left  = i == 2 ? 12.0f : 0.0f;
        posters.draw(vg, i, px, y, column + (i == 0 ? 0.0f : 1.0f), height, left, right, right, left,
                     this->alpha * 0.95f);
    }
    // The collection's colour over the posters, strong on the left where the name is
    fillRounded(vg, x, y, width, height, 12,
                nvgLinearGradient(vg, x, y, x + width, y, a(withAlpha(colorA, 0.93f)), a(withAlpha(colorB, 0.25f))));
    fillRounded(vg, x, y, width, height, 12,
                nvgLinearGradient(vg, x, y + height * 0.4f, x, y + height, a(nvgRGBA(0, 0, 0, 0)),
                                  a(nvgRGBA(0, 0, 0, 120))));
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

// PersonCard

PersonCard::PersonCard(const tsvitch::TmdbPerson& person) {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setWidth(WIDTH);
    this->setMarginRight(8);
    photo = new brls::Image();
    photo->setWidth(64);
    photo->setHeight(64);
    photo->setCornerRadius(32);
    photo->setScalingType(brls::ImageScalingType::FILL);
    // TMDB's photos are portraits: the face is in their top part
    photo->setImageAlign(brls::ImageAlignment::TOP);
    photo->setBackgroundColor(nvgRGBA(255, 255, 255, 28));
    this->addView(photo);
    if (!person.photo.empty()) ImageHelper::with(photo)->load(person.photo);
    // Left aligned with at most the card's width (the card centers them): borealis shortens a long name with an
    // ellipsis only when the text is not centered
    auto* name = makeLabel(13, nvgRGB(255, 255, 255), person.name);
    name->setSingleLine(true);
    name->setMaxWidth(WIDTH);
    name->setMarginTop(6);
    auto* role = makeLabel(12, nvgRGBA(255, 255, 255, 150), person.role);
    role->setSingleLine(true);
    role->setMaxWidth(WIDTH);
    this->addView(name);
    this->addView(role);
}

PersonCard::~PersonCard() { ImageHelper::clear(photo); }

// DiscoverShelfView

DiscoverShelfView::DiscoverShelfView(tsvitch::discover::Shelf shelf, OpenItem openItem, OpenCollection openCollection,
                                     bool compact)
    : shelf(std::move(shelf)), openItem(std::move(openItem)), openCollection(std::move(openCollection)),
      compact(compact) {
    this->setAxis(brls::Axis::COLUMN);
    this->setMarginTop(compact ? 12 : 18);

    auto* title = makeLabel(compact ? 17 : 22, brls::Application::getTheme().getColor("brls/text"), this->shelf.title);
    title->setMarginLeft(compact ? 0 : 30);
    title->setMarginBottom(compact ? 6 : 12);
    title->setSingleLine(true);
    this->addView(title);

    float height = this->shelf.kind == tsvitch::discover::Shelf::POSTERS  ? (compact ? 206 : 292)
                   : this->shelf.kind == tsvitch::discover::Shelf::GENRES ? GenreTile::HEIGHT + 12
                                                                          : CoverCard::HEIGHT + 12;
    scroller = new brls::HScrollingFrame();
    scroller->setHeight(height);
    scroller->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroller->setScrollingIndicatorVisible(false);
    row = new brls::Box(brls::Axis::ROW);
    if (compact)
        row->setPadding(4, 4, 4, 4);
    else
        row->setPadding(6, 30, 6, 30);
    scroller->setContentView(row);
    this->addView(scroller);
}

DiscoverShelfView::~DiscoverShelfView() { *alive = false; }

void DiscoverShelfView::buildCards() {
    if (built) return;
    built = true;
    if (shelf.kind == tsvitch::discover::Shelf::POSTERS) {
        for (size_t i = 0; i < shelf.items.size(); i++) {
            auto* card = RecyclingGridItemLiveVideoCard::createPoster();
            card->setWidth(compact ? 90 : 146);
            card->setMarginRight(compact ? 12 : 16);
            card->setPosterHeight(compact ? 135 : 219);
            // The card shows the name without the provider's tags; the detail screen gets the item as it is
            auto shown  = shelf.items[i];
            shown.title = tsvitch::discover::cleanTitle(shown.title);
            card->setChannel(shown, false, false);
            if (i < shelf.notes.size() && !shelf.notes[i].empty()) card->setNote(shelf.notes[i]);
            if (i < shelf.progress.size() && !shelf.progress[i].empty()) card->setWatchUrl(shelf.progress[i]);
            auto item = shelf.items[i];
            auto open = openItem;
            card->registerClickAction([item, open](brls::View*) {
                if (open) open(item);
                return true;
            });
            row->addView(card);
        }
    } else {
        for (const auto& collection : shelf.collections) {
            auto open = openCollection;
            auto onClick = [collection, open]() {
                if (open) open(collection);
            };
            if (shelf.kind == tsvitch::discover::Shelf::GENRES)
                row->addView(new GenreTile(collection, onClick));
            else
                row->addView(new CoverCard(collection, onClick));
        }
    }
}

void DiscoverShelfView::setNear(bool value) {
    pendingChange = false;
    if (value == near) return;
    near = value;
    if (near) this->buildCards();
    if (!row) return;
    for (auto* child : row->getChildren()) {
        if (auto* card = dynamic_cast<RecyclingGridItemLiveVideoCard*>(child)) {
            if (near)
                card->loadPicture();
            else
                card->releasePicture();
        } else if (auto* cover = dynamic_cast<CoverCard*>(child)) {
            cover->setVisibleOnScreen(near);
        }
    }
}

void DiscoverShelfView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                             brls::FrameContext* ctx) {
    // Near the screen: cards and pictures; far away (three screens): the pictures are freed
    bool wanted = near ? onScreen(y, height, NEAR_DISTANCE * 3) : onScreen(y, height, NEAR_DISTANCE);
    if (wanted != near && !pendingChange) {
        pendingChange = true;
        // Views are added on the next frame, never while this one is drawn
        auto isAlive = alive;
        brls::sync([this, wanted, isAlive]() {
            if (*isAlive) this->setNear(wanted);
        });
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* DiscoverShelfView::getDefaultFocus() {
    // Moving onto a shelf that was never near the screen: its cards are made now
    if (!built) this->setNear(true);
    return brls::Box::getDefaultFocus();
}

void DiscoverShelfView::refreshWatchStates() {
    if (!row) return;
    for (auto* child : row->getChildren())
        if (auto* card = dynamic_cast<RecyclingGridItemLiveVideoCard*>(child)) card->showWatchState();
}
