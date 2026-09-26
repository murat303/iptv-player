#pragma once

#include <borealis/core/view.hpp>

/// A thin progress bar: an orange fill over a faint track. Without a known progress (setProgress with a
/// negative value) a short segment keeps sliding across, so the user still sees that something is going on.
class ProgressLine : public brls::View {
public:
    ProgressLine();

    /// 0..1, or negative for "unknown"
    void setProgress(float value);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    static brls::View* create();

private:
    float progress = -1;
};
