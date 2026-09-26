#include "view/progress_line.hpp"

#include <algorithm>
#include <cmath>

#include <borealis/core/time.hpp>

ProgressLine::ProgressLine() { this->setFocusable(false); }

void ProgressLine::setProgress(float value) { progress = value < 0 ? -1 : std::min(1.0f, value); }

void ProgressLine::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                        brls::FrameContext* ctx) {
    float radius = height / 2;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, radius);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 48));
    nvgFill(vg);

    float start = 0, length = 0;
    if (progress >= 0) {
        length = width * progress;
    } else {
        // a third of the bar slides from left to right, once every 1.4 s
        double phase = std::fmod(brls::getCPUTimeUsec() / 1400000.0, 1.0);
        length       = width * 0.3f;
        start        = static_cast<float>(phase * (width + length)) - length;
    }
    float left  = std::max(x, x + start);
    float right = std::min(x + width, x + start + length);
    if (right - left < 0.5f) return;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, left, y, right - left, height, radius);
    nvgFillColor(vg, nvgRGB(255, 145, 0));
    nvgFill(vg);
}

brls::View* ProgressLine::create() { return new ProgressLine(); }
