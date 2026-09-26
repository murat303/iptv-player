#pragma once

#include <borealis/core/view.hpp>

/// A turning ring for loading screens: an orange arc runs around a faint track and keeps growing and shrinking
class LoadingRing : public brls::View {
public:
    LoadingRing();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    static brls::View* create();
};
