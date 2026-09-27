#include "utils/xtream_account.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <borealis/core/application.hpp>
#include <borealis/core/box.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>
#include <cpr/error.h>
#include <fmt/format.h>
#include <memory>

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

std::string formatClock(int64_t unixTime) {
    std::time_t time = static_cast<std::time_t>(unixTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char text[16];
    std::strftime(text, sizeof(text), "%H:%M:%S", &local);
    return text;
}

std::string formatBytes(size_t bytes) {
    if (bytes >= 1024 * 1024) return fmt::format("{:.1f} MB", bytes / 1048576.0);
    return fmt::format("{} KB", std::max<size_t>(1, (bytes + 1023) / 1024));
}

std::string formatSeconds(int ms) { return brls::getStr("tsvitch/connection/seconds", fmt::format("{:.1f}", ms / 1000.0)); }

// Why a request got no usable answer; empty when it got one
std::string failure(const XtreamRequestLog& log) {
    using E = cpr::ErrorCode;
    if (log.error == -1) return "tsvitch/connection/error/setup"_i18n;
    if (log.error != 0) {
        switch (static_cast<E>(log.error)) {
            case E::OPERATION_TIMEDOUT:
                return "tsvitch/connection/error/timeout"_i18n;
            case E::COULDNT_CONNECT:
                return "tsvitch/connection/error/connect"_i18n;
            case E::COULDNT_RESOLVE_HOST:
            case E::COULDNT_RESOLVE_PROXY:
                return "tsvitch/connection/error/host"_i18n;
            case E::SSL_CONNECT_ERROR:
                return "tsvitch/connection/error/ssl"_i18n;
            case E::GOT_NOTHING:
            case E::SEND_ERROR:
            case E::RECV_ERROR:
            case E::PARTIAL_FILE:
            case E::WEIRD_SERVER_REPLY:
                return "tsvitch/connection/error/dropped"_i18n;
            default:
                return "tsvitch/connection/error/other"_i18n;
        }
    }
    if (log.status != 200) return brls::getStr("tsvitch/connection/error/http", log.status);
    if (log.cut) return "tsvitch/connection/error/dropped"_i18n;
    return "";
}

// "OK · 0.4 s · 12 KB", or why it failed, with the retries and the wait in the app
std::string describe(const XtreamRequestLog& log) {
    std::string reason = failure(log);
    std::string text   = reason.empty()
                             ? brls::getStr("tsvitch/connection/ok", formatSeconds(log.durationMs), formatBytes(log.bytes))
                             : brls::getStr("tsvitch/connection/failed", reason, formatSeconds(log.durationMs));
    if (log.attempts > 1) text += "  ·  " + brls::getStr("tsvitch/connection/attempts", log.attempts);
    if (log.waitMs >= 1000) text += "  ·  " + brls::getStr("tsvitch/connection/waited", formatSeconds(log.waitMs));
    return text;
}

class ConnectionTestView : public brls::Box {
public:
    ConnectionTestView(const std::string& results, const std::string& recent) {
        this->setAxis(brls::Axis::COLUMN);
        this->setWidth(1000);
        this->setPadding(30, 40, 24, 40);
        auto* title = new brls::Label();
        title->setFontSize(22);
        title->setText("tsvitch/connection/title"_i18n);
        auto* body = new brls::Label();
        body->setFontSize(18);
        body->setMarginTop(14);
        body->setText(results);
        auto* heading = new brls::Label();
        heading->setFontSize(18);
        heading->setMarginTop(20);
        heading->setText("tsvitch/connection/recent"_i18n);
        auto* list = new brls::Label();
        list->setFontSize(15);
        list->setMarginTop(6);
        list->setTextColor(nvgRGB(180, 185, 194));
        list->setText(recent);
        for (brls::View* view : std::initializer_list<brls::View*>{title, body, heading, list}) this->addView(view);
    }
};

struct TestStep {
    std::string label, action;
    std::vector<std::pair<std::string, std::string>> params;
};

struct TestRun {
    std::vector<TestStep> steps;
    std::vector<XtreamRequestLog> logs;
};

bool testRunning = false;

void showTestResults(const TestRun& run) {
    bool failed = false, slow = false;
    std::string results;
    for (size_t i = 0; i < run.steps.size(); i++) {
        const auto& log = run.logs[i];
        failed          = failed || !failure(log).empty();
        slow            = slow || log.durationMs > 5000 || log.attempts > 1;
        results += run.steps[i].label + ":  " + describe(log) + "\n";
    }
    results += "\n" + std::string(failed ? "tsvitch/connection/summary_fail"_i18n
                                   : slow ? "tsvitch/connection/summary_slow"_i18n
                                          : "tsvitch/connection/summary_good"_i18n);
    std::string recent;
    auto logs = TsVitchClient::recentXtreamRequests();
    for (size_t i = 0; i < logs.size() && i < 10; i++) {
        const auto& log = logs[i];
        recent += formatClock(log.time) + "   " + (log.action.empty() ? "user_info" : log.action) + "   " + describe(log) +
                  "\n";
    }
    if (recent.empty()) recent = "tsvitch/connection/none"_i18n;
    auto* dialog = new brls::Dialog(new ConnectionTestView(results, recent));
    dialog->addButton("hints/ok"_i18n, []() {});
    dialog->open();
}

// One request after the other, like the app: the provider gets one small request at a time
void runTestStep(const std::shared_ptr<TestRun>& run) {
    if (run->logs.size() >= run->steps.size()) {
        testRunning = false;
        showTestResults(*run);
        return;
    }
    const auto& step = run->steps[run->logs.size()];
    TsVitchClient::testXtreamRequest(step.action, step.params, [run](XtreamRequestLog log) {
        run->logs.push_back(std::move(log));
        runTestStep(run);
    });
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

void showXtreamConnectionTest() {
    brls::Application::notify("tsvitch/connection/running"_i18n);
    if (testRunning) return;
    testRunning = true;
    auto run    = std::make_shared<TestRun>();
    run->steps.push_back({"tsvitch/connection/account"_i18n, "", {}});
    run->steps.push_back({"tsvitch/connection/categories"_i18n, "get_series_categories", {}});
    std::string series = ProgramConfig::instance().getSettingItem(SettingItem::XTREAM_LAST_SERIES, std::string{});
    if (!series.empty()) run->steps.push_back({"tsvitch/connection/series"_i18n, "get_series_info", {{"series_id", series}}});
    runTestStep(run);
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
