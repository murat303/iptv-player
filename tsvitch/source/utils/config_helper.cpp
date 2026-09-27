#ifdef IOS
#include <CoreFoundation/CoreFoundation.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#if defined(_WIN32)
#include <shlobj.h>
#endif
#endif

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <set>
#include <filesystem>
#include <fstream>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/cache_helper.hpp>
#include <borealis/core/touch/pan_gesture.hpp>
#include <borealis/views/edit_text_dialog.hpp>
#include <cpr/filesystem.h>

#include "tsvitch.h"
#include "utils/number_helper.hpp"
#include "utils/thread_helper.hpp"
#include "utils/image_helper.hpp"
#include "utils/config_helper.hpp"
#include "utils/text_fold.hpp"
#include "utils/crash_helper.hpp"
#include "utils/vibration_helper.hpp"
#include "utils/activity_helper.hpp"
#include "activity/live_player_activity.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"

#include "config/m3u8_config.h"
#include "api/tsvitch/util/http.hpp"

#ifdef PS4
#include <orbis/SystemService.h>
#include <orbis/Sysmodule.h>
#include <arpa/inet.h>

extern "C" {
extern int ps4_mpv_use_precompiled_shaders;
extern int ps4_mpv_dump_shaders;
extern in_addr_t primary_dns;
extern in_addr_t secondary_dns;
}
#endif

#ifdef _WIN32
#include <winsock2.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

using namespace brls::literals;

// Maps the OS locale (LC_ALL/LC_MESSAGES/LANG) to a supported app locale,
// falling back to English. Used on desktop so the app follows the system language.
static std::string detectSystemLocale() {
    const char* env = std::getenv("LC_ALL");
    if (!env || !*env) env = std::getenv("LC_MESSAGES");
    if (!env || !*env) env = std::getenv("LANG");

    std::string lower;
    if (env) {
        for (char c : std::string(env)) {
            if (c == '.' || c == '@') break;  // drop encoding/modifier (e.g. ".UTF-8")
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    auto startsWith = [&](const char* prefix) { return lower.rfind(prefix, 0) == 0; };

    if (startsWith("pt")) return brls::LOCALE_PT_BR;
    if (startsWith("it")) return brls::LOCALE_IT;
    if (startsWith("ja")) return brls::LOCALE_JA;
    if (startsWith("ko")) return brls::LOCALE_Ko;
    if (startsWith("zh")) {
        if (lower.find("tw") != std::string::npos || lower.find("hk") != std::string::npos ||
            lower.find("hant") != std::string::npos)
            return brls::LOCALE_ZH_HANT;
        return brls::LOCALE_ZH_HANS;
    }
    return brls::LOCALE_EN_US;
}

std::unordered_map<SettingItem, ProgramOption> ProgramConfig::SETTING_MAP = {

    {SettingItem::CUSTOM_UPDATE_API, {"custom_update_api", {}, {}, 0}},
    {SettingItem::APP_LANG,
     {"app_lang",
      {
          // English by default; only languages that have the app's own texts
          brls::LOCALE_EN_US, "tr", brls::LOCALE_IT, brls::LOCALE_PT_BR,
#if defined(__SWITCH__) || defined(__PSV__) || defined(PS4)
          brls::LOCALE_AUTO,
#endif
      },
      {},
      0}},
    {SettingItem::APP_THEME, {"app_theme", {"auto", "light", "dark"}, {}, 0}},
    {SettingItem::APP_RESOURCES, {"app_resources", {}, {}, 0}},
    {SettingItem::APP_UI_SCALE,
     {"app_ui_scale",
      {"544p", "720p", "900p", "1080p"},
      {},
#ifdef __PSV__
      0}},
#else
      1}},
#endif
    {SettingItem::KEYMAP, {"keymap", {"xbox", "ps", "keyboard"}, {}, 0}},
    {SettingItem::HOME_WINDOW_STATE, {"home_window_state", {}, {}, 0}},
    {SettingItem::DLNA_IP, {"dlna_ip", {}, {}, 0}},
    {SettingItem::DLNA_NAME, {"dlna_name", {}, {}, 0}},
    {SettingItem::PLAYER_ASPECT, {"player_aspect", {"-1", "-2", "-3", "4:3", "16:9"}, {}, 0}},

    {SettingItem::APP_SWAP_ABXY, {"app_swap_abxy", {}, {}, 0}},
    {SettingItem::GAMEPAD_VIBRATION, {"gamepad_vibration", {}, {}, 1}},
#if defined(IOS) || defined(__PSV__)
    {SettingItem::HIDE_BOTTOM_BAR, {"hide_bottom_bar", {}, {}, 1}},
#else
    {SettingItem::HIDE_BOTTOM_BAR, {"hide_bottom_bar", {}, {}, 0}},
#endif
    {SettingItem::HIDE_FPS, {"hide_fps", {}, {}, 1}},
#if defined(__APPLE__) || !defined(NDEBUG)

    {SettingItem::FULLSCREEN, {"fullscreen", {}, {}, 0}},
#else

    {SettingItem::FULLSCREEN, {"fullscreen", {}, {}, 1}},
#endif
    {SettingItem::HISTORY_REPORT, {"history_report", {}, {}, 1}},
    {SettingItem::PLAYER_AUTO_PLAY, {"player_auto_play", {}, {}, 1}},
    {SettingItem::PLAYER_BOTTOM_BAR, {"player_bottom_bar", {}, {}, 1}},
    {SettingItem::PLAYER_HIGHLIGHT_BAR, {"player_highlight_bar", {}, {}, 0}},
    {SettingItem::PLAYER_SKIP_OPENING_CREDITS, {"player_skip_opening_credits", {}, {}, 1}},
    {SettingItem::PLAYER_LOW_QUALITY, {"player_low_quality", {}, {}, 1}},
#if defined(IOS) || defined(__PSV__) || defined(__SWITCH__)
    {SettingItem::PLAYER_HWDEC, {"player_hwdec", {}, {}, 1}},
#else
    {SettingItem::PLAYER_HWDEC, {"player_hwdec", {}, {}, 0}},
#endif
    {SettingItem::PLAYER_HWDEC_CUSTOM, {"player_hwdec_custom", {}, {}, 0}},
    {SettingItem::PLAYER_EXIT_FULLSCREEN_ON_END, {"player_exit_fullscreen_on_end", {}, {}, 1}},
    {SettingItem::PLAYER_OSD_TV_MODE, {"player_osd_tv_mode", {}, {}, 0}},
    {SettingItem::OPENCC_ON, {"opencc", {}, {}, 1}},

    {SettingItem::SEARCH_TV_MODE, {"search_tv_mode", {}, {}, 1}},
    {SettingItem::TLS_VERIFY,
     {"tls_verify",
      {},
      {},
#if defined(__PSV__) || defined(__SWITCH__) || defined(PS4)
      0}},
#else
      1}},
#endif

#if defined(__PSV__)
    {SettingItem::PLAYER_INMEMORY_CACHE, {"player_inmemory_cache", {"0MB", "1MB", "5MB", "10MB"}, {0, 1, 5, 10}, 0}},
#elif defined(__SWITCH__)
    {SettingItem::PLAYER_INMEMORY_CACHE,
     {"player_inmemory_cache", {"0MB", "10MB", "20MB", "50MB", "100MB"}, {0, 10, 20, 50, 100}, 0}},
#else
    {SettingItem::PLAYER_INMEMORY_CACHE,
     {"player_inmemory_cache", {"0MB", "10MB", "20MB", "50MB", "100MB"}, {0, 10, 20, 50, 100}, 1}},
#endif
    {
        SettingItem::PLAYER_DEFAULT_SPEED,
        {"player_default_speed",
         {"4.0x", "3.0x", "2.0x", "1.75x", "1.5x", "1.25x", "1.0x", "0.75x", "0.5x", "0.25x"},
         {400, 300, 200, 175, 150, 125, 100, 75, 50, 25},
         2},
    },
    {SettingItem::PLAYER_VOLUME, {"player_volume", {}, {}, 0}},
    {SettingItem::TEXTURE_CACHE_NUM, {"texture_cache_num", {}, {}, 0}},
    {SettingItem::VIDEO_QUALITY, {"video_quality", {}, {}, 116}},
    {SettingItem::IMAGE_REQUEST_THREADS,
     {"image_request_threads",
#if defined(__SWITCH__) || defined(__PSV__)
      {"1", "2", "3", "4"},
      {1, 2, 3, 4},
      3}},
#else
      {"1", "2", "3", "4", "8", "12", "16"},
      {1, 2, 3, 4, 8, 12, 16},
      3}},
#endif

    {SettingItem::LIMITED_FPS, {"limited_fps", {"0", "30", "60", "90", "120"}, {0, 30, 60, 90, 120}, 0}},
    {SettingItem::DEACTIVATED_TIME, {"deactivated_time", {}, {}, 0}},
    {SettingItem::DEACTIVATED_FPS, {"deactivated_fps", {}, {}, 0}},
    {SettingItem::DLNA_PORT, {"dlna_port", {}, {}, 0}},
    {SettingItem::PLAYER_STRATEGY, {"player_strategy", {"rcmd", "next", "loop", "single"}, {0, 1, 2, 3}, 0}},
    {SettingItem::PLAYER_BRIGHTNESS, {"player_brightness", {}, {}, 0}},
    {SettingItem::PLAYER_CONTRAST, {"player_contrast", {}, {}, 0}},
    {SettingItem::PLAYER_SATURATION, {"player_saturation", {}, {}, 0}},
    {SettingItem::PLAYER_HUE, {"player_hue", {}, {}, 0}},
    {SettingItem::PLAYER_GAMMA, {"player_gamma", {}, {}, 0}},
    {SettingItem::MINIMUM_WINDOW_WIDTH, {"minimum_window_width", {"480"}, {480}, 0}},
    {SettingItem::MINIMUM_WINDOW_HEIGHT, {"minimum_window_height", {"270"}, {270}, 0}},
    {SettingItem::ON_TOP_WINDOW_WIDTH, {"on_top_window_width", {"480"}, {480}, 0}},
    {SettingItem::ON_TOP_WINDOW_HEIGHT, {"on_top_window_height", {"270"}, {270}, 0}},
    {SettingItem::ON_TOP_MODE, {"on_top_mode", {"off", "always", "auto"}, {0, 1, 2}, 0}},
    {SettingItem::SCROLL_SPEED, {"scroll_speed", {}, {}, 0}},
    {SettingItem::GROUP_SELECTED_INDEX, {"group_selected_index", {}, {}, 0}},
    {SettingItem::UP_FILTER, {"up_filter", {}, {}, 0}},
    {SettingItem::M3U8_URL_ITEM, {"m3u8_url", {}, {}, 0}},
    {SettingItem::PROXY_URL_ITEM, {"proxy_url", {}, {}, 0}},
    {SettingItem::M3U8_TIMEOUT, {"m3u8_timeout", {"60", "120", "300", "600"}, {60000, 120000, 300000, 600000}, 2}}, // Default: 5 minuti
    
    // IPTV Mode Selection
    {SettingItem::IPTV_MODE, {"iptv_mode", {"M3U8 Playlist", "Xtream Codes"}, {0, 1}, 0}}, // Default: M3U8
    
    // Xtream Codes IPTV Settings
    {SettingItem::XTREAM_SERVER_URL, {"xtream_server_url", {}, {}, 0}},
    {SettingItem::XTREAM_USERNAME, {"xtream_username", {}, {}, 0}},
    {SettingItem::XTREAM_PASSWORD, {"xtream_password", {}, {}, 0}},
    {SettingItem::XTREAM_ENABLED, {"xtream_enabled", {}, {}, 0}}, // 0 = disabled, 1 = enabled
    {SettingItem::XTREAM_CONTENT_TYPE, {"xtream_content_type", {}, {}, 0}}, // 0 = Live TV, 1 = Movies (VOD)
    {SettingItem::LAST_UPDATE_VERSION_SEEN, {"last_update_version_seen", {}, {}, 0}},

    // Parental control
    {SettingItem::PARENTAL_ENABLED, {"parental_enabled", {}, {}, 1}}, // default enabled
    {SettingItem::PARENTAL_PIN, {"parental_pin", {}, {}, 0}},
    {SettingItem::PARENTAL_CONFIGURED, {"parental_configured", {}, {}, 0}},
    {SettingItem::PARENTAL_LOCKED_CATEGORIES, {"parental_locked_categories", {}, {}, 0}},
    {SettingItem::KNOWN_CATEGORIES, {"known_categories", {}, {}, 0}},
    {SettingItem::XTREAM_SORT_MODE, {"xtream_sort_mode", {}, {}, 0}},
    {SettingItem::PLAYER_LANGUAGE, {"player_language", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_LANGUAGE, {"player_sub_language", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_SIZE, {"player_sub_size", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_COLOR, {"player_sub_color", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_BACKGROUND, {"player_sub_background", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_POSITION, {"player_sub_position", {}, {}, 0}},
    {SettingItem::PLAYER_AUTO_NEXT, {"player_auto_next", {}, {}, 1}},
    {SettingItem::PLAYER_NEXT_AT, {"player_next_at", {}, {}, 2}},
    {SettingItem::XTREAM_LAST_SERIES, {"xtream_last_series", {}, {}, 0}},
    {SettingItem::PLAYER_AUDIO_TITLE, {"player_audio_title", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_TITLE, {"player_sub_title", {}, {}, 0}},
    {SettingItem::PLAYER_SUB_FORCED, {"player_sub_forced", {}, {}, 0}},
    {SettingItem::XTREAM_ACCOUNT_CHECKED, {"xtream_account_checked", {}, {}, 0}},
    {SettingItem::XTREAM_AUTO_REFRESH, {"xtream_auto_refresh", {}, {}, 0}},
};

ProgramConfig::ProgramConfig() = default;

ProgramConfig::ProgramConfig(const ProgramConfig& conf) {
    this->setting = conf.setting;
    this->device  = conf.device;
    this->client  = conf.client;
}

ProgramConfig::~ProgramConfig() {
#ifdef IOS
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (hasExitSubscription) {
        brls::Application::getExitEvent()->unsubscribe(exitEventSubscription);
    }
#endif
}

void ProgramConfig::setProgramConfig(const ProgramConfig& conf) {
    this->setting = conf.setting;
    this->client  = conf.client;
    this->device  = conf.device;
    // Only the keys: the values include the Xtream password
    std::string settingKeys;
    for (auto it = conf.setting.begin(); it != conf.setting.end(); ++it) settingKeys += it.key() + " ";
    brls::Logger::info("setting keys: {}", settingKeys);
}

std::string ProgramConfig::getClientID() {
    if (this->client.empty()) {
        this->client = fmt::format("{}.{}", tsvitch::getRandomNumber(), tsvitch::getUnixTime());
        this->save();
    }
    return this->client;
}

std::string ProgramConfig::getDeviceID() {
    return this->device;
}

void ProgramConfig::setDeviceID(const std::string& deviceId) {
    this->device = deviceId;
    this->save();
}

void ProgramConfig::loadHomeWindowState() {
    std::string homeWindowStateData = getSettingItem(SettingItem::HOME_WINDOW_STATE, std::string{""});

    if (homeWindowStateData.empty()) return;

    uint32_t hWidth, hHeight;
    int hXPos, hYPos;
    int monitor;

    sscanf(homeWindowStateData.c_str(), "%d,%ux%u,%dx%d", &monitor, &hWidth, &hHeight, &hXPos, &hYPos);

    if (hWidth == 0 || hHeight == 0) return;

    uint32_t minWidth  = getIntOption(SettingItem::MINIMUM_WINDOW_WIDTH);
    uint32_t minHeight = getIntOption(SettingItem::MINIMUM_WINDOW_HEIGHT);
    if (hWidth < minWidth) hWidth = minWidth;
    if (hHeight < minHeight) hHeight = minHeight;

    VideoContext::sizeH        = hHeight;
    VideoContext::sizeW        = hWidth;
    VideoContext::posX         = (float)hXPos;
    VideoContext::posY         = (float)hYPos;
    VideoContext::monitorIndex = monitor;

    brls::Logger::info("Load window state: {}x{},{}x{}", hWidth, hHeight, hXPos, hYPos);
}

void ProgramConfig::saveHomeWindowState() {
    if (std::isnan(VideoContext::posX) || std::isnan(VideoContext::posY)) return;
    auto videoContext = brls::Application::getPlatform()->getVideoContext();

    uint32_t width  = VideoContext::sizeW;
    uint32_t height = VideoContext::sizeH;
    int xPos        = VideoContext::posX;
    int yPos        = VideoContext::posY;

    int monitor = videoContext->getCurrentMonitorIndex();
    if (width == 0) width = brls::Application::ORIGINAL_WINDOW_WIDTH;
    if (height == 0) height = brls::Application::ORIGINAL_WINDOW_HEIGHT;
    brls::Logger::info("Save window state: {},{}x{},{}x{}", monitor, width, height, xPos, yPos);
    setSettingItem(SettingItem::HOME_WINDOW_STATE, fmt::format("{},{}x{},{}x{}", monitor, width, height, xPos, yPos));
}

void ProgramConfig::load() {
    this->importLegacyConfig();
    const std::string path = this->getConfigDir() + "/" + CONFIG_FILE;

    std::ifstream readFile(path);
    if (readFile) {
        try {
            nlohmann::json content;
            readFile >> content;
            readFile.close();
            this->setProgramConfig(content.get<ProgramConfig>());
        } catch (const std::exception& e) {
            brls::Logger::error("ProgramConfig::load: {}", e.what());
        }
        brls::Logger::info("Load config from: {}", path);
    }

    this->m3u8Url = getSettingItem(SettingItem::M3U8_URL_ITEM, this->getM3U8Url());
    this->proxyUrl = getSettingItem(SettingItem::PROXY_URL_ITEM, this->getProxyUrl());
    
    // Configura o proxy se estiver definido
    if (!this->proxyUrl.empty()) {
        tsvitch::HTTP::setProxy(this->proxyUrl);
    }

#ifdef IOS
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    brls::DesktopPlatform::GAMEPAD_DB = getConfigDir() + "/gamecontrollerdb.txt";
#endif

    std::string customThemeID = getSettingItem(SettingItem::APP_RESOURCES, std::string{""});
    if (!customThemeID.empty()) {
        for (auto& theme : customThemes) {
            if (theme.id == customThemeID) {
                brls::View::CUSTOM_RESOURCES_PATH = theme.path;
                break;
            }
        }
        if (brls::View::CUSTOM_RESOURCES_PATH.empty()) {
            brls::Logger::warning("Custom theme not found: {}", customThemeID);
        }
    }

    std::string UIScale = getSettingItem(SettingItem::APP_UI_SCALE, std::string{""});
    if (UIScale == "544p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 960;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 544;
    } else if (UIScale == "720p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 1280;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 720;
    } else if (UIScale == "900p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 1600;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 900;
    } else if (UIScale == "1080p") {
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 1920;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 1080;
    } else {
#ifdef __PSV__
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 960;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 544;
#else
        brls::Application::ORIGINAL_WINDOW_WIDTH  = 1280;
        brls::Application::ORIGINAL_WINDOW_HEIGHT = 720;
#endif
    }

    MPVCore::AUTO_PLAY = getBoolOption(SettingItem::PLAYER_AUTO_PLAY);

    MPVCore::VIDEO_SPEED = getIntOption(SettingItem::PLAYER_DEFAULT_SPEED);

    MPVCore::VIDEO_ASPECT = getSettingItem(SettingItem::PLAYER_ASPECT, std::string{"-1"});

    MPVCore::VIDEO_BRIGHTNESS = getSettingItem(SettingItem::PLAYER_BRIGHTNESS, 0);
    MPVCore::VIDEO_CONTRAST   = getSettingItem(SettingItem::PLAYER_CONTRAST, 0);
    MPVCore::VIDEO_SATURATION = getSettingItem(SettingItem::PLAYER_SATURATION, 0);
    MPVCore::VIDEO_HUE        = getSettingItem(SettingItem::PLAYER_HUE, 0);
    MPVCore::VIDEO_GAMMA      = getSettingItem(SettingItem::PLAYER_GAMMA, 0);

    VibrationHelper::GAMEPAD_VIBRATION = getBoolOption(SettingItem::GAMEPAD_VIBRATION);

    ImageHelper::REQUEST_THREADS = getIntOption(SettingItem::IMAGE_REQUEST_THREADS);

    brls::Application::setFPSStatus(!getBoolOption(SettingItem::HIDE_FPS));

    VideoContext::FULLSCREEN = getBoolOption(SettingItem::FULLSCREEN);

    VideoView::BOTTOM_BAR = getBoolOption(SettingItem::PLAYER_BOTTOM_BAR);

    VideoView::HIGHLIGHT_PROGRESS_BAR = getBoolOption(SettingItem::PLAYER_HIGHLIGHT_BAR);

#ifdef __PSV__
    MPVCore::HARDWARE_DEC = true;
#else
    MPVCore::HARDWARE_DEC = getBoolOption(SettingItem::PLAYER_HWDEC);
#endif

    MPVCore::PLAYER_HWDEC_METHOD = getSettingItem(SettingItem::PLAYER_HWDEC_CUSTOM, MPVCore::PLAYER_HWDEC_METHOD);

    VideoView::EXIT_FULLSCREEN_ON_END = getBoolOption(SettingItem::PLAYER_EXIT_FULLSCREEN_ON_END);

    MPVCore::INMEMORY_CACHE = getIntOption(SettingItem::PLAYER_INMEMORY_CACHE);

    brls::Label::OPENCC_ON = getBoolOption(SettingItem::OPENCC_ON);

    MPVCore::LOW_QUALITY = getBoolOption(SettingItem::PLAYER_LOW_QUALITY);

#ifdef _WIN32
    int scrollSpeed = getSettingItem(SettingItem::SCROLL_SPEED, 150);
#else
    int scrollSpeed = getSettingItem(SettingItem::SCROLL_SPEED, 100);
#endif
    brls::PanGestureRecognizer::panFactor = scrollSpeed * 0.01f;

    std::set<std::string> i18nData{brls::LOCALE_AUTO, brls::LOCALE_EN_US, brls::LOCALE_IT, brls::LOCALE_PT_BR, "tr"};
    // English unless the user picked another language
    std::string langData = getSettingItem(SettingItem::APP_LANG, brls::LOCALE_EN_US);

    if (langData != brls::LOCALE_AUTO && i18nData.count(langData)) {
        brls::Platform::APP_LOCALE_DEFAULT = langData;
    } else {
#if !defined(__SWITCH__) && !defined(__PSV__) && !defined(PS4)
        // Follow the system language on desktop (borealis' AUTO only reads BOREALIS_LANG,
        // so we map the OS locale ourselves, falling back to English).
        brls::Platform::APP_LOCALE_DEFAULT = detectSystemLocale();
#endif
    }
#ifdef IOS
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)

    loadHomeWindowState();
#endif

    brls::Application::setLimitedFPS(getSettingItem(SettingItem::LIMITED_FPS, 0));

    int deactivatedTime = getSettingItem(SettingItem::DEACTIVATED_TIME, 0);
    if (deactivatedTime > 0) {
        brls::Application::setAutomaticDeactivation(true);
        brls::Application::setDeactivatedTime(deactivatedTime);
    }

    brls::Application::setDeactivatedFPS(getSettingItem(SettingItem::DEACTIVATED_FPS, 5));

    brls::Application::getWindowCreationDoneEvent()->subscribe([this]() {
        if (getBoolOption(SettingItem::APP_SWAP_ABXY)) {
            brls::Application::setSwapInputKeys(!brls::Application::isSwapInputKeys());
        }

        std::string themeData = getSettingItem(SettingItem::APP_THEME, std::string{"auto"});
        if (themeData == "light") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
        } else if (themeData == "dark") {
            brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        }

#if defined(__PSV__) || defined(PS4)
        brls::TextureCache::instance().cache.setCapacity(1);
#else
        brls::TextureCache::instance().cache.setCapacity(getSettingItem(SettingItem::TEXTURE_CACHE_NUM, 200));
#endif

        MPVCore::VIDEO_VOLUME = getSettingItem(SettingItem::PLAYER_VOLUME, 100);

#ifdef IOS
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
        int minWidth  = getIntOption(SettingItem::MINIMUM_WINDOW_WIDTH);
        int minHeight = getIntOption(SettingItem::MINIMUM_WINDOW_HEIGHT);
        brls::Application::getPlatform()->setWindowSizeLimits(minWidth, minHeight, 0, 0);
        checkOnTop();
#endif

        brls::Application::getPlatform()->getInputManager()->getKeyboardKeyStateChanged()->subscribe(
            [](brls::KeyState state) {
                if (!state.pressed) return;
                switch (state.key) {
#ifndef __APPLE__
                    case brls::BRLS_KBD_KEY_F11:
                        ProgramConfig::instance().toggleFullscreen();
                        break;
#endif
                    case brls::BRLS_KBD_KEY_F: {
                        auto activityStack  = brls::Application::getActivitiesStack();
                        brls::Activity* top = activityStack[activityStack.size() - 1];
                        if (!dynamic_cast<brls::EditTextDialog*>(top->getContentView())) {
                            ProgramConfig::instance().toggleFullscreen();
                        }
                        break;
                    }
                    case brls::BRLS_KBD_KEY_SPACE: {
                        auto activityStack  = brls::Application::getActivitiesStack();
                        brls::Activity* top = activityStack[activityStack.size() - 1];
                        VideoView* video    = dynamic_cast<VideoView*>(top->getContentView()->getView("video"));
                        if (video) {
                            video->togglePlay();
                        }
                        break;
                    }
                    default:
                        break;
                }
            });
    });

#ifdef IOS
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)

    exitEventSubscription = brls::Application::getExitEvent()->subscribe([this]() { saveHomeWindowState(); });
    hasExitSubscription = true;
#endif
}

ProgramOption ProgramConfig::getOptionData(SettingItem item) { return SETTING_MAP[item]; }

size_t ProgramConfig::getIntOptionIndex(SettingItem item) {
    auto optionData = getOptionData(item);
    if (setting.contains(optionData.key)) {
        try {
            int option = this->setting.at(optionData.key).get<int>();
            for (size_t i = 0; i < optionData.rawOptionList.size(); i++) {
                if (optionData.rawOptionList[i] == option) return i;
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", optionData.key, e.what());
            return optionData.defaultOption;
        }
    }
    return optionData.defaultOption;
}

int ProgramConfig::getIntOption(SettingItem item) {
    auto optionData = getOptionData(item);
    if (setting.contains(optionData.key)) {
        try {
            return this->setting.at(optionData.key).get<int>();
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", optionData.key, e.what());
            return optionData.rawOptionList[optionData.defaultOption];
        }
    }
    return optionData.rawOptionList[optionData.defaultOption];
}

bool ProgramConfig::getBoolOption(SettingItem item) {
    auto optionData = getOptionData(item);
    if (setting.contains(optionData.key)) {
        try {
            return this->setting.at(optionData.key).get<bool>();
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", optionData.key, e.what());
            return optionData.defaultOption;
        }
    }
    return optionData.defaultOption;
}

int ProgramConfig::getStringOptionIndex(SettingItem item) {
    auto optionData = getOptionData(item);
    if (setting.contains(optionData.key)) {
        try {
            std::string option = this->setting.at(optionData.key).get<std::string>();
            for (size_t i = 0; i < optionData.optionList.size(); ++i)
                if (optionData.optionList[i] == option) return i;
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", optionData.key, e.what());
            return optionData.defaultOption;
        }
    }
    return optionData.defaultOption;
}

void ProgramConfig::save() {
    const std::string path = this->getConfigDir() + "/" + CONFIG_FILE;

#ifndef IOS
    cpr::fs::create_directories(this->getConfigDir());
#endif
    nlohmann::json content(*this);
    std::ofstream writeFile(path);
    if (!writeFile) {
        brls::Logger::error("Cannot write config to: {}", path);
        return;
    }
    writeFile << content.dump(2);
    writeFile.close();
    brls::Logger::info("Write config to: {}", path);
}

void ProgramConfig::checkOnTop() {
    switch (getIntOption(SettingItem::ON_TOP_MODE)) {
        case 0:

            brls::Application::getPlatform()->setWindowAlwaysOnTop(false);
            return;
        case 1:

            brls::Application::getPlatform()->setWindowAlwaysOnTop(true);
            return;
        case 2: {
            double factor     = brls::Application::getPlatform()->getVideoContext()->getScaleFactor();
            uint32_t minWidth = ProgramConfig::instance().getIntOption(SettingItem::ON_TOP_WINDOW_WIDTH) * factor + 0.1;
            uint32_t minHeight =
                ProgramConfig::instance().getIntOption(SettingItem::ON_TOP_WINDOW_HEIGHT) * factor + 0.1;
            bool onTop = brls::Application::windowWidth <= minWidth || brls::Application::windowHeight <= minHeight;
            brls::Application::getPlatform()->setWindowAlwaysOnTop(onTop);
            break;
        }
        default:
            break;
    }
}

void ProgramConfig::init() {
    brls::Logger::info("{} {}", APP_TITLE, APPVersion::instance().git_tag);
    tsvitch::initCrashDump();

    brls::Application::getWindowSizeChangedEvent()->subscribe([]() { ProgramConfig::instance().checkOnTop(); });

    curl_global_init(CURL_GLOBAL_DEFAULT);
    cpr::async::startup(THREAD_POOL_MIN_THREAD_NUM, THREAD_POOL_MAX_THREAD_NUM, std::chrono::milliseconds(5000));

#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) brls::Logger::error("WSAStartup failed with error: {}", result);
#endif
#if defined(_MSC_VER)
#elif defined(__PSV__)
#elif defined(PS4)
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0) brls::Logger::error("cannot load net module");
    primary_dns                     = inet_addr(primaryDNSStr.c_str());
    secondary_dns                   = inet_addr(secondaryDNSStr.c_str());
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders            = 0;

    brls::sync([]() { sceSystemServiceHideSplashScreen(); });
#else
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        brls::Logger::info("Current working directory: {}", cwd);
    }
#endif

    this->loadCustomThemes();

    this->load();

    brls::FontLoader::USER_FONT_PATH = getConfigDir() + "/font.ttf";
    brls::FontLoader::USER_ICON_PATH = getConfigDir() + "/icon.ttf";

    if (access(brls::FontLoader::USER_ICON_PATH.c_str(), F_OK) == -1) {
#if defined(__PSV__) || defined(PS4)
        brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
#else
        std::string icon = getSettingItem(SettingItem::KEYMAP, std::string{"xbox"});
        if (icon == "xbox") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_xbox.ttf");
        } else if (icon == "ps") {
            brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_ps.ttf");
        } else {
            if (getBoolOption(SettingItem::APP_SWAP_ABXY)) {
                brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard_swap.ttf");
            } else {
                brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
            }
        }
#endif
    }

    brls::FontLoader::USER_EMOJI_PATH = getConfigDir() + "/emoji.ttf";
    if (access(brls::FontLoader::USER_EMOJI_PATH.c_str(), F_OK) == -1) {
        brls::FontLoader::USER_EMOJI_PATH = BRLS_ASSET("font/emoji.ttf");
    }
}

std::string ProgramConfig::getHomePath() {
#if defined(__SWITCH__)
    return "/";
#elif defined(_WIN32)
    return std::string(getenv("HOMEPATH"));
#else
    return std::string(getenv("HOME"));
#endif
}

std::string ProgramConfig::getConfigDir() {
#ifdef __SWITCH__
    // Everything of the app sits next to its .nro
    return "/switch/iptv-player";
#elif defined(PS4)
    return "/data/iptv-player";
#elif defined(__PSV__)
    return "ux0:/data/iptv-player";
#elif defined(IOS)
    CFURLRef homeURL = CFCopyHomeDirectoryURL();
    if (homeURL != nullptr) {
        char buffer[PATH_MAX];
        if (CFURLGetFileSystemRepresentation(homeURL, true, reinterpret_cast<UInt8*>(buffer), sizeof(buffer))) {
        }
        CFRelease(homeURL);
        return std::string{buffer} + "/Library/Preferences";
    }
    return "../Library/Preferences";
#else
#ifdef _DEBUG
    char currentPathBuffer[PATH_MAX];
    std::string currentPath = getcwd(currentPathBuffer, sizeof(currentPathBuffer));
#ifdef _WIN32
    return currentPath + "\\config\\iptv-player";
#else
    return currentPath + "/config/iptv-player";
#endif
#else
#ifdef __APPLE__
    return std::string(getenv("HOME")) + "/Library/Application Support/iptv-player";
#endif
#ifdef __linux__
    std::string config = "";
    char* config_home  = getenv("XDG_CONFIG_HOME");
    if (config_home) config = std::string(config_home);
    if (config.empty()) config = std::string(getenv("HOME")) + "/.config";
    return config + "/iptv-player";
#endif
#ifdef _WIN32
    WCHAR wpath[MAX_PATH];
    std::vector<char> lpath(MAX_PATH);
    SHGetSpecialFolderPathW(0, wpath, CSIDL_LOCAL_APPDATA, false);
    WideCharToMultiByte(CP_UTF8, 0, wpath, std::wcslen(wpath), lpath.data(), lpath.size(), nullptr, nullptr);
    return std::string(lpath.data()) + "\\muratgokce\\iptv-player";
#endif
#endif
#endif
}

std::string ProgramConfig::getLegacyConfigDir() {
#ifdef __SWITCH__
    return "/config/tsvitch";
#elif defined(__linux__) && !defined(_DEBUG) && !defined(IOS)
    std::string dir = getConfigDir();
    return dir.substr(0, dir.size() - std::string("iptv-player").size()) + "tsvitch";
#else
    return "";
#endif
}

namespace {

// Big blocks: the Switch's SD card is slow with the small default buffers (the lists of a provider are MBs)
bool copyFileContents(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::FILE* in = std::fopen(from.string().c_str(), "rb");
    if (!in) return false;
    std::FILE* out = std::fopen(to.string().c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    size_t n;
    while ((n = std::fread(buffer.data(), 1, buffer.size(), in)) > 0) {
        if (std::fwrite(buffer.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    ok = ok && !std::ferror(in);
    std::fclose(in);
    return std::fclose(out) == 0 && ok;
}

// Copies a folder tree; files that already exist in the target are kept
size_t copyTree(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    size_t copied = 0;
    std::filesystem::create_directories(to, ec);
    for (auto it = std::filesystem::recursive_directory_iterator(from, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::filesystem::path target = to / std::filesystem::relative(it->path(), from, ec);
        if (it->is_directory(ec)) {
            std::filesystem::create_directories(target, ec);
        } else if (!std::filesystem::exists(target, ec) && copyFileContents(it->path(), target)) {
            copied++;
        }
    }
    return copied;
}

}  // namespace

// The first start takes over the files of TsVitch, whose folder this app used before 1.0: the settings with the
// IPTV account, favorites, history, positions, the provider's lists and the download list. Everything is copied,
// nothing is moved, so TsVitch keeps its files. Downloaded videos stay where they are: the copied download list
// still points at them.
void ProgramConfig::importLegacyConfig() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path from = getLegacyConfigDir();
    const fs::path to   = getConfigDir();
    if (from.empty() || fs::exists(to / CONFIG_FILE, ec) || !fs::exists(from / "tsvitch_config.json", ec)) return;

    brls::Logger::info("Importing the settings of {}", from.string());
    fs::create_directories(to, ec);
    size_t copied = 0;
    // The names are read first: one folder is moved away below, which must not happen during the listing
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(from, ec)) entries.push_back(entry);
    for (const auto& entry : entries) {
        std::error_code entryError;
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(entryError)) {
            // The provider's lists (downloading them again takes minutes) belong to this app's earlier builds only,
            // so they move (instantly) instead of being copied (MBs on the SD card, before the screen shows).
            if (name == "xtream") {
                fs::rename(entry.path(), to / name, entryError);
                if (entryError) copied += copyTree(entry.path(), to / name);
                else copied++;
            } else if (name == "theme") {
                copied += copyTree(entry.path(), to / name);
            }
        } else if (name == "tsvitch_config.json") {
            nlohmann::json content;
            {
                std::ifstream in(entry.path());
                content = nlohmann::json::parse(in, nullptr, false);
            }
            if (content.is_discarded() || !content.is_object()) {
                brls::Logger::warning("Import: the old settings are damaged");
                continue;
            }
            // Builds before 1.0 opened in Turkish when no language was picked: their users keep it
            auto setting = content.find("setting");
            if (setting != content.end() && setting->is_object() && !setting->contains("app_lang")) {
                for (const char* key : {"player_auto_next", "player_sub_size", "player_sub_color", "xtream_account_checked"}) {
                    if (setting->contains(key)) {
                        (*setting)["app_lang"] = "tr";
                        break;
                    }
                }
            }
            std::ofstream out(to / CONFIG_FILE, std::ios::trunc);
            out << content.dump(2);
            if (out.good()) {
                copied++;
                importedLegacy = true;
            }
        } else if (name != "subfont.ttf" && !fs::exists(to / name, entryError)) {
            // subfont.ttf is made again from the system font when a subtitle needs it
            if (copyFileContents(entry.path(), to / name)) copied++;
        }
    }
    // The download list; the videos and covers it points at stay in the old folder
    const fs::path downloads = from / "downloads" / "downloads.json";
    if (fs::exists(downloads, ec)) {
        fs::create_directories(to / "downloads", ec);
        if (!fs::exists(to / "downloads" / "downloads.json", ec) &&
            copyFileContents(downloads, to / "downloads" / "downloads.json"))
            copied++;
    }
    brls::Logger::info("Imported {} files into {}", copied, to.string());
}

void ProgramConfig::exit(char* argv[]) {
    cpr::async::cleanup();
    curl_global_cleanup();

#ifdef _WIN32
    WSACleanup();
#endif
#ifdef IOS
#elif defined(PS4)
#elif __PSV__
#elif defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    if (!brls::DesktopPlatform::RESTART_APP) return;
#ifdef __linux__
    char filePath[PATH_MAX + 1];
    ssize_t count = readlink("/proc/self/exe", filePath, PATH_MAX);
    if (count <= 0)
        strcpy(filePath, argv[0]);
    else
        filePath[count] = 0;
#else
    char* filePath = argv[0];
#endif

    brls::Logger::info("Restart app {}", filePath);

    execv(filePath, argv);
#endif
}

void ProgramConfig::loadCustomThemes() {
    customThemes.clear();
    std::string directoryPath = getConfigDir() + "/theme";
    if (!cpr::fs::exists(directoryPath)) return;

    for (const auto& entry : cpr::fs::directory_iterator(getConfigDir() + "/theme")) {
#if USE_BOOST_FILESYSTEM
        if (!cpr::fs::is_directory(entry)) continue;
#else
        if (!entry.is_directory()) continue;
#endif
        std::string subDirectory = entry.path().string();
        std::string jsonFilePath = subDirectory + "/resources_meta.json";
        if (!cpr::fs::exists(jsonFilePath)) continue;

        std::ifstream readFile(jsonFilePath);
        if (readFile) {
            try {
                nlohmann::json content;
                readFile >> content;
                readFile.close();
                CustomTheme customTheme;
                customTheme.path = subDirectory + "/";
                customTheme.id   = entry.path().filename().string();
                content.get_to(customTheme);
                customThemes.emplace_back(customTheme);
                brls::Logger::info("Load custom theme \"{}\" from: {}", customTheme.name, jsonFilePath);
            } catch (const std::exception& e) {
                brls::Logger::error("CustomTheme::load: {}", e.what());
                continue;
            }
        }
    }
}

std::vector<CustomTheme> ProgramConfig::getCustomThemes() { return customThemes; }

std::string ProgramConfig::getM3U8Url() {
    if (m3u8Url.empty())
        return M3U8_URL_VALUE;
    else
        return this->m3u8Url;
}
//https://raw.githubusercontent.com/Free-TV/IPTV/refs/heads/master/playlists/playlist_italy.m3u8
void ProgramConfig::setM3U8Url(const std::string& url) {
    this->m3u8Url = url;
    brls::Logger::info("setM3U8Url: {}", m3u8Url);
    setSettingItem(SettingItem::M3U8_URL_ITEM, m3u8Url);
    if (m3u8Url.empty()) {
        m3u8Url = M3U8_URL_VALUE;
    }
    GA("m3u8_url", {{"url", m3u8Url}});
}

std::string ProgramConfig::getProxyUrl() {
    return this->proxyUrl;
}

void ProgramConfig::setProxyUrl(const std::string& url) {
    this->proxyUrl = url;
    brls::Logger::info("setProxyUrl: {}", proxyUrl);
    setSettingItem(SettingItem::PROXY_URL_ITEM, proxyUrl);
    
    // Configura o proxy para todas as requisições HTTP
    tsvitch::HTTP::setProxy(proxyUrl);
    
    // Dispara evento para notificar mudança no proxy
    OnProxyUrlChanged.fire();
    
    GA("proxy_url", {{"url", proxyUrl}});
}
// Xtream Codes IPTV getters and setters
std::string ProgramConfig::getXtreamServerUrl() {
    return getSettingItem(SettingItem::XTREAM_SERVER_URL, std::string(""));
}

void ProgramConfig::setXtreamServerUrl(const std::string& url) {
    setSettingItem(SettingItem::XTREAM_SERVER_URL, url);
    brls::Logger::info("setXtreamServerUrl: {}", url);
}

std::string ProgramConfig::getXtreamUsername() {
    return getSettingItem(SettingItem::XTREAM_USERNAME, std::string(""));
}

void ProgramConfig::setXtreamUsername(const std::string& username) {
    setSettingItem(SettingItem::XTREAM_USERNAME, username);
    brls::Logger::info("setXtreamUsername: {}", username);
}

std::string ProgramConfig::getXtreamPassword() {
    return getSettingItem(SettingItem::XTREAM_PASSWORD, std::string(""));
}

void ProgramConfig::setXtreamPassword(const std::string& password) {
    setSettingItem(SettingItem::XTREAM_PASSWORD, password);
    brls::Logger::info("setXtreamPassword: [hidden]");
}

bool ProgramConfig::getXtreamEnabled() {
    return getSettingItem(SettingItem::XTREAM_ENABLED, 0) == 1;
}

void ProgramConfig::setXtreamEnabled(bool enabled) {
    setSettingItem(SettingItem::XTREAM_ENABLED, enabled ? 1 : 0);
    brls::Logger::info("setXtreamEnabled: {}", enabled);
}

int ProgramConfig::getXtreamContentType() {
    return getSettingItem(SettingItem::XTREAM_CONTENT_TYPE, 0);
}

void ProgramConfig::setXtreamContentType(int contentType) {
    setSettingItem(SettingItem::XTREAM_CONTENT_TYPE, contentType);
    brls::Logger::info("setXtreamContentType: {}", contentType);
}

// ===== Parental control =====

bool ProgramConfig::isParentalEnabled() {
    return getSettingItem(SettingItem::PARENTAL_ENABLED, 1) == 1;
}

void ProgramConfig::setParentalEnabled(bool enabled) {
    setSettingItem(SettingItem::PARENTAL_ENABLED, enabled ? 1 : 0);
}

std::string ProgramConfig::getParentalPin() {
    std::string pin = getSettingItem(SettingItem::PARENTAL_PIN, std::string(""));
    return pin.empty() ? std::string("0000") : pin;  // default PIN
}

void ProgramConfig::setParentalPin(const std::string& pin) {
    setSettingItem(SettingItem::PARENTAL_PIN, pin.empty() ? std::string("0000") : pin);
}

bool ProgramConfig::isAdultCategory(const std::string& name) {
    if (name.empty()) return false;
    std::string lower = tsvitch::foldForSearch(name);
    static const char* keywords[] = {"adult", "adulto", "xxx", "+18", "18+", "porn", "erotic", "erotico",
                                     "erótico", "erotik", "yetiskin", " sex", "sexo", "hot "};
    for (const char* kw : keywords) {
        if (lower.find(kw) != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> ProgramConfig::getLockedCategories() {
    std::vector<std::string> result;
    std::string raw = getSettingItem(SettingItem::PARENTAL_LOCKED_CATEGORIES, std::string(""));
    if (raw.empty()) return result;
    try {
        auto arr = nlohmann::json::parse(raw, nullptr, false);
        if (arr.is_array())
            for (const auto& v : arr)
                if (v.is_string()) result.push_back(v.get<std::string>());
    } catch (...) {
    }
    return result;
}

bool ProgramConfig::isCategoryLocked(const std::string& name) {
    if (!isParentalEnabled()) return false;
    // Se o usuário configurou a lista, respeita-a; senão, usa a detecção automática
    if (getSettingItem(SettingItem::PARENTAL_CONFIGURED, 0) == 1) {
        auto locked = getLockedCategories();
        return std::find(locked.begin(), locked.end(), name) != locked.end();
    }
    return isAdultCategory(name);
}

void ProgramConfig::setCategoryLocked(const std::string& name, bool locked) {
    auto list = getLockedCategories();
    // Se ainda não configurado, parte do default (categorias adultas conhecidas)
    if (getSettingItem(SettingItem::PARENTAL_CONFIGURED, 0) != 1) {
        list.clear();
        for (const auto& c : getKnownCategories())
            if (isAdultCategory(c)) list.push_back(c);
    }
    auto it = std::find(list.begin(), list.end(), name);
    if (locked && it == list.end()) {
        list.push_back(name);
    } else if (!locked && it != list.end()) {
        list.erase(it);
    }
    nlohmann::json arr = list;
    setSettingItem(SettingItem::PARENTAL_LOCKED_CATEGORIES, arr.dump());
    setSettingItem(SettingItem::PARENTAL_CONFIGURED, 1);
}

std::vector<std::string> ProgramConfig::getKnownCategories() {
    std::vector<std::string> result;
    std::string raw = getSettingItem(SettingItem::KNOWN_CATEGORIES, std::string(""));
    if (raw.empty()) return result;
    try {
        auto arr = nlohmann::json::parse(raw, nullptr, false);
        if (arr.is_array())
            for (const auto& v : arr)
                if (v.is_string()) result.push_back(v.get<std::string>());
    } catch (...) {
    }
    return result;
}

void ProgramConfig::addKnownCategories(const std::vector<std::string>& names) {
    auto known = getKnownCategories();
    std::set<std::string> set(known.begin(), known.end());
    bool changed = false;
    for (const auto& n : names) {
        if (!n.empty() && set.insert(n).second) changed = true;
    }
    if (!changed) return;
    nlohmann::json arr = std::vector<std::string>(set.begin(), set.end());
    setSettingItem(SettingItem::KNOWN_CATEGORIES, arr.dump());
}

void ProgramConfig::resetApp() {
    brls::Logger::warning("ProgramConfig::resetApp: wiping all configuration");
    this->setting = nlohmann::json::object();
    this->client.clear();
    this->device.clear();
    this->m3u8Url.clear();
    this->proxyUrl.clear();
    this->save();
}

void ProgramConfig::toggleFullscreen() {
    bool value = !getBoolOption(SettingItem::FULLSCREEN);
    setSettingItem(SettingItem::FULLSCREEN, value);
    VideoContext::FULLSCREEN = value;
    brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
    GA("player_setting", {{"fullscreen", value ? "true" : "false"}});
}