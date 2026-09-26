#include "view/loading_ring.hpp"

#include <algorithm>
#include <cmath>

#include <borealis/core/time.hpp>

LoadingRing::LoadingRing() { this->setFocusable(false); }

void LoadingRing::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                       brls::FrameContext* ctx) {
    float thickness = std::max(3.0f, std::min(width, height) * 0.09f);
    float radius    = std::min(width, height) / 2 - thickness;
    float cx        = x + width / 2;
    float cy        = y + height / 2;
    double seconds  = brls::getCPUTimeUsec() / 1000000.0;

    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, radius);
    nvgStrokeWidth(vg, thickness);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 38));
    nvgStroke(vg);

    // About one turn a second; the arc breathes between a short dash and three quarters of the ring
    float start = static_cast<float>(std::fmod(seconds * 2 * NVG_PI * 1.1, 2 * NVG_PI));
    float sweep = NVG_PI * (0.85f + 0.65f * static_cast<float>(std::sin(seconds * 2.6)));
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, radius, start, start + sweep, NVG_CW);
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeWidth(vg, thickness);
    nvgStrokeColor(vg, nvgRGB(255, 145, 0));
    nvgStroke(vg);
    nvgLineCap(vg, NVG_BUTT);
}

brls::View* LoadingRing::create() { return new LoadingRing(); }
