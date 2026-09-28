#pragma once

#include <functional>
#include <borealis/core/box.hpp>

/// A box of texts that takes the focus only while one of them is cut short (the detail screen's plot and cast:
/// A shows all of it); otherwise the focus moves past it
class ExpandableBox : public brls::Box {
public:
    std::function<bool()> expandable;

    brls::View* getDefaultFocus() override { return expandable && expandable() ? this : nullptr; }

    static brls::View* create() { return new ExpandableBox(); }
};
