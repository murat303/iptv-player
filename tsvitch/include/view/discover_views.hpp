#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis/core/application.hpp>
#include <borealis/core/box.hpp>
#include <borealis/views/h_scrolling_frame.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>

#include "utils/discover.hpp"
#include "view/recycling_grid.hpp"

/// Pictures a view draws itself (rounded, cropped, tinted): hidden brls::Image views load them through ImageHelper
/// and the view paints their textures. They load when the view comes near the screen and are freed far from it.
class PictureSet {
public:
    explicit PictureSet(std::vector<std::string> urls);
    ~PictureSet();

    void load();
    void release();
    bool isLoaded() const { return loaded; }
    size_t size() const { return images.size(); }

    /// Paints picture i cropped to fill the rectangle (nothing while it loads); false when it is not there yet.
    /// The corners are rounded one by one: top left, top right, bottom right, bottom left.
    bool draw(NVGcontext* vg, size_t i, float x, float y, float width, float height, float radius, float alpha);
    bool draw(NVGcontext* vg, size_t i, float x, float y, float width, float height, float topLeft, float topRight,
              float bottomRight, float bottomLeft, float alpha);

private:
    std::vector<std::string> urls;
    std::vector<brls::Image*> images;
    bool loaded = false;
};

/// The big picture at the top of the discovery screen: a backdrop, what it is and why it is there
class DiscoverHero : public brls::Box {
public:
    DiscoverHero(const tsvitch::discover::Hero& hero, std::function<void(const tsvitch::LiveM3u8&)> open);
    ~DiscoverHero() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    void setVisibleOnScreen(bool visible);

private:
    tsvitch::LiveM3u8 item;
    PictureSet backdrop;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool pendingChange          = false;
};

/// A genre as a coloured tile with its name and how many titles it has
class GenreTile : public brls::Box {
public:
    GenreTile(const tsvitch::discover::Collection& genre, std::function<void()> onClick);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    static constexpr float WIDTH  = 210;
    static constexpr float HEIGHT = 112;

private:
    NVGcolor colorA, colorB;
};

/// A genre tile in a grid: the "Genres" group of the movie and series lists
class GenreGridCell : public RecyclingGridItem {
public:
    GenreGridCell();

    void setGenre(const tsvitch::discover::Collection& genre);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    static RecyclingGridItem* create();

private:
    NVGcolor colorA, colorB;
    brls::Label* name  = nullptr;
    brls::Label* count = nullptr;
};

/// A collection as a wide card: three of its posters side by side, tinted with its colours, its name over them
class CoverCard : public brls::Box {
public:
    CoverCard(const tsvitch::discover::Collection& collection, std::function<void()> onClick);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    void setVisibleOnScreen(bool visible);

    static constexpr float WIDTH  = 300;
    static constexpr float HEIGHT = 170;

private:
    NVGcolor colorA, colorB;
    PictureSet posters;
};


/// One shelf of the discovery screen: a title and a row that scrolls sideways. Its cards are made when the shelf
/// first comes near the screen, and their pictures are freed when it is far away again.
class DiscoverShelfView : public brls::Box {
public:
    using OpenItem       = std::function<void(const tsvitch::LiveM3u8&)>;
    using OpenCollection = std::function<void(const tsvitch::discover::Collection&)>;

    // compact: smaller posters, no side margins (a row inside the detail screen)
    DiscoverShelfView(tsvitch::discover::Shelf shelf, OpenItem openItem, OpenCollection openCollection,
                      bool compact = false);
    ~DiscoverShelfView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    brls::View* getDefaultFocus() override;

    /// The watched marks and bars of its cards again (after a detail screen)
    void refreshWatchStates();

    const std::string& shelfId() const { return shelf.id; }

private:
    void buildCards();
    void setNear(bool near);

    tsvitch::discover::Shelf shelf;
    OpenItem openItem;
    OpenCollection openCollection;
    brls::HScrollingFrame* scroller = nullptr;
    brls::Box* row                  = nullptr;
    bool built                      = false;
    bool near                       = false;
    bool compact                    = false;
    bool pendingChange              = false;
    std::shared_ptr<bool> alive     = std::make_shared<bool>(true);
};
