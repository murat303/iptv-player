#pragma once

#include "view/auto_tab_frame.hpp"

class RecyclingGrid;
class CustomButton;

class HomeHistory : public AttachedView {
public:
    HomeHistory();

    // void onRecommendVideoList(const bilibili::RecommendVideoListResultWrapper &result) override;

    ~HomeHistory();

    static View *create();

    void onCreate() override;

    void onShow() override;

    // Entering the tab focuses the first video, not the button above the list
    brls::View* getDefaultFocus() override;

    void onError(const std::string &error);

    void refreshRecent();

    // Shows the history with the row at focus focused (if the focus is in the list)
    void showRecent(size_t focus);

    // Y: the focused video leaves the history
    void removeFocused();

    void confirmClear();

    void toggleFavorite();

    void downloadVideo();

private:
    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/history/recyclingGrid");
    BRLS_BIND(CustomButton, clearButton, "home/history/clear");
};