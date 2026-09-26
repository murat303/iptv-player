#include "utils/xtream_account.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/views/dialog.hpp>

#include "api/tsvitch.h"
#include "api/tsvitch/result/xtream_detail.h"
#include "utils/config_helper.hpp"

using namespace brls::literals;

namespace tsvitch {

namespace {

constexpr int64_t DAY = 24 * 60 * 60;

std::string formatDate(int64_t unixTime) {
    std::time_t time = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%d.%m.%Y", &local);
    return text;
}

// Whole days until the end; negative once it has passed
int64_t daysLeft(int64_t expiresAt) {
    int64_t seconds = expiresAt - static_cast<int64_t>(std::time(nullptr));
    return seconds >= 0 ? seconds / DAY : -((-seconds + DAY - 1) / DAY);
}

std::string statusText(std::string status) {
    std::transform(status.begin(), status.end(), status.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char* known : {"active", "expired", "banned", "disabled"})
        if (status == known) return brls::getStr(std::string("tsvitch/account/status_") + known);
    return status.empty() ? "-" : status;
}

void openDialog(const std::string& text) {
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("hints/ok"_i18n, []() {});
    dialog->open();
}

}  // namespace

void showXtreamAccountInfo() {
    // The answer can take a while (the server gets one request at a time): more clicks meanwhile open nothing
    static bool loading = false;
    brls::Application::notify("tsvitch/account/loading"_i18n);
    if (loading) return;
    loading = true;
    TsVitchClient::get_xtream_account_info(
        [](const XtreamAccountInfo& info) {
            loading = false;
            std::vector<std::string> lines = {brls::getStr("tsvitch/account/status", statusText(info.status))};
            if (info.expiresAt > 0) {
                std::string end = brls::getStr("tsvitch/account/expires", formatDate(info.expiresAt));
                int64_t days    = daysLeft(info.expiresAt);
                if (days >= 0) end += "  ·  " + brls::getStr("tsvitch/account/days_left", days);
                lines.push_back(end);
            } else {
                lines.push_back("tsvitch/account/unlimited"_i18n);
            }
            if (info.maxConnections > 0)
                lines.push_back(brls::getStr("tsvitch/account/connections", info.activeConnections,
                                             info.maxConnections));
            lines.push_back(brls::getStr("tsvitch/account/trial",
                                         info.trial ? "tsvitch/account/yes"_i18n : "tsvitch/account/no"_i18n));
            if (info.createdAt > 0) lines.push_back(brls::getStr("tsvitch/account/created", formatDate(info.createdAt)));

            std::string text = "tsvitch/account/title"_i18n + "\n";
            for (const auto& line : lines) text += "\n" + line;
            openDialog(text);
        },
        [](const std::string&, int) {
            loading = false;
            openDialog("tsvitch/account/error"_i18n);
        });
}

void checkXtreamExpiry() {
    auto& config  = ProgramConfig::instance();
    int64_t now   = static_cast<int64_t>(std::time(nullptr));
    int64_t last  = config.getSettingItem(SettingItem::XTREAM_ACCOUNT_CHECKED, int64_t{0});
    if (now - last < DAY) return;
    // Saved before asking: a server that does not answer is not asked again at every start
    config.setSettingItem(SettingItem::XTREAM_ACCOUNT_CHECKED, now);

    TsVitchClient::get_xtream_account_info([](const XtreamAccountInfo& info) {
        if (info.expiresAt <= 0) return;
        int64_t days = daysLeft(info.expiresAt);
        if (days > 7) return;
        if (days < 0)
            brls::Application::notify("tsvitch/account/ended"_i18n);
        else if (days == 0)
            brls::Application::notify("tsvitch/account/ends_today"_i18n);
        else
            brls::Application::notify(brls::getStr("tsvitch/account/ends_soon", days, formatDate(info.expiresAt)));
    });
}

}  // namespace tsvitch
