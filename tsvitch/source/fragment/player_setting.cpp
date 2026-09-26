

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_slider.hpp>
#include <borealis/views/cells/cell_input.hpp>

#include "utils/config_helper.hpp"
#include "utils/shader_helper.hpp"
#include "utils/number_helper.hpp"
#include "utils/activity_helper.hpp"

#include "fragment/player_setting.hpp"

#include "view/button_close.hpp"

#include "view/video_view.hpp"
#include "view/selector_cell.hpp"
#include "view/svg_image.hpp"
#include "view/mpv_core.hpp"

using namespace brls::literals;

/// Readable name of an ISO 639 language code ("tur" -> "Türkçe"); unknown codes are shown in capitals
static std::string languageName(std::string code) {
    static const std::unordered_map<std::string, std::string> names = {
        {"tur", "Türkçe"},   {"tr", "Türkçe"},    {"eng", "English"},    {"en", "English"},   {"ger", "Deutsch"},
        {"deu", "Deutsch"},  {"de", "Deutsch"},   {"fre", "Français"},   {"fra", "Français"}, {"fr", "Français"},
        {"spa", "Español"},  {"es", "Español"},   {"ita", "Italiano"},   {"it", "Italiano"},  {"por", "Português"},
        {"pt", "Português"}, {"rus", "Русский"},  {"ru", "Русский"},     {"ara", "Arabic"},   {"ar", "Arabic"},
        {"jpn", "日本語"},   {"ja", "日本語"},    {"kor", "한국어"},     {"ko", "한국어"},    {"chi", "中文"},
        {"zho", "中文"},     {"zh", "中文"},      {"dut", "Nederlands"}, {"nld", "Nederlands"}, {"nl", "Nederlands"},
        {"pol", "Polski"},   {"pl", "Polski"},    {"hin", "Hindi"},      {"hi", "Hindi"},     {"gre", "Ελληνικά"},
        {"ell", "Ελληνικά"}, {"el", "Ελληνικά"},  {"swe", "Svenska"},    {"sv", "Svenska"},   {"kur", "Kurdî"},
        {"ku", "Kurdî"},     {"aze", "Azərbaycan"}, {"az", "Azərbaycan"}};
    for (auto& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = names.find(code);
    if (it != names.end()) return it->second;
    for (auto& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return code;
}

static std::string describeTrack(const MPVCore::Track& track) {
    std::string name = track.lang.empty() || track.lang == "und" ? "" : languageName(track.lang);
    if (!track.title.empty() && track.title != name) name += name.empty() ? track.title : " - " + track.title;
    if (name.empty()) name = brls::getStr("tsvitch/player/tracks/track", track.id);
    if (track.channels == 6)
        name += " (5.1)";
    else if (track.channels == 8)
        name += " (7.1)";
    return name;
}

PlayerSetting::PlayerSetting() {
    this->inflateFromXMLRes("xml/fragment/player_setting.xml");
    brls::Logger::debug("Fragment PlayerSetting: create");

    setupTrackSetting();
    setupCommonSetting();

    this->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    closebtn->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("Fragment PlayerSetting: delete"); }

brls::View* PlayerSetting::create() { return new PlayerSetting(); }

bool PlayerSetting::isTranslucent() { return true; }

brls::View* PlayerSetting::getDefaultFocus() { return this->settings->getDefaultFocus(); }

void PlayerSetting::setupTrackSetting() {
    // type: "audio" / "sub"; the chosen language is remembered for the next videos
    auto setup = [](brls::DetailCell* cell, const std::string& title, const std::string& type, bool allowOff) {
        cell->setText(title);
        auto tracks          = MPVCore::instance().getTracks(type);
        std::string selected = allowOff ? "hints/off"_i18n : "";
        for (const auto& track : tracks)
            if (track.selected) selected = describeTrack(track);
        cell->setDetailText(tracks.empty() ? "tsvitch/player/tracks/none"_i18n : selected);
        cell->registerClickAction([cell, title, type, allowOff](brls::View*) {
            auto tracks = MPVCore::instance().getTracks(type);
            if (tracks.empty()) return true;
            std::vector<std::string> names;
            int current = 0;
            if (allowOff) {
                names.push_back("hints/off"_i18n);
            }
            for (const auto& track : tracks) {
                if (track.selected) current = static_cast<int>(names.size());
                names.push_back(describeTrack(track));
            }
            BaseDropdown::text(
                title, names,
                [cell, type, names, tracks, allowOff](int index) {
                    if (index < 0 || index >= (int)names.size()) return;
                    size_t trackIndex = allowOff ? index - 1 : index;
                    bool off          = allowOff && index == 0;
                    std::string lang  = off ? "no" : tracks[trackIndex].lang == "und" ? "" : tracks[trackIndex].lang;
                    // The language is remembered for the next videos, the track changes right away
                    if (type == "audio") {
                        ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_LANGUAGE, lang);
                        MPVCore::instance().setPreferredLanguages(lang, "");
                        MPVCore::instance().command_async("set", "aid", std::to_string(tracks[trackIndex].id));
                    } else {
                        ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_SUB_LANGUAGE, lang);
                        MPVCore::instance().setPreferredLanguages("", lang);
                        MPVCore::instance().command_async("set", "sid",
                                                          off ? "no" : std::to_string(tracks[trackIndex].id));
                    }
                    cell->setDetailText(names[index]);
                },
                current);
            return true;
        });
    };
    setup(btnAudioTrack, "tsvitch/player/tracks/audio"_i18n, "audio", false);
    setup(btnSubtitleTrack, "tsvitch/player/tracks/subtitle"_i18n, "sub", true);

    // Look of the subtitles: saved for every video and applied right away
    auto style = [](TsVitchSelectorCell* cell, const std::string& title, const std::vector<std::string>& options,
                    SettingItem item, int defaultValue) {
        int value = std::clamp(ProgramConfig::instance().getSettingItem(item, defaultValue), 0, (int)options.size() - 1);
        cell->init(title, options, value, [item](int selected) {
            ProgramConfig::instance().setSettingItem(item, selected);
            MPVCore::instance().applySubtitleStyle();
        });
    };
    style(btnSubSize, "tsvitch/player/tracks/sub_size"_i18n,
          {"tsvitch/player/tracks/size_small"_i18n, "tsvitch/player/tracks/size_normal"_i18n,
           "tsvitch/player/tracks/size_big"_i18n, "tsvitch/player/tracks/size_huge"_i18n},
          SettingItem::PLAYER_SUB_SIZE, 1);
    style(btnSubColor, "tsvitch/player/tracks/sub_color"_i18n,
          {"tsvitch/player/tracks/color_white"_i18n, "tsvitch/player/tracks/color_yellow"_i18n},
          SettingItem::PLAYER_SUB_COLOR, 0);
    style(btnSubBackground, "tsvitch/player/tracks/sub_background"_i18n,
          {"hints/off"_i18n, "tsvitch/player/tracks/background_half"_i18n,
           "tsvitch/player/tracks/background_black"_i18n},
          SettingItem::PLAYER_SUB_BACKGROUND, 0);
    style(btnSubPosition, "tsvitch/player/tracks/sub_position"_i18n,
          {"tsvitch/player/tracks/position_bottom"_i18n, "tsvitch/player/tracks/position_higher"_i18n,
           "tsvitch/player/tracks/position_high"_i18n},
          SettingItem::PLAYER_SUB_POSITION, 0);

    // Delay for badly timed subtitles: only for the playing video
    static const std::vector<double> delays = {-5, -3, -2, -1, -0.5, 0, 0.5, 1, 2, 3, 5};
    std::vector<std::string> names;
    size_t current     = 5;
    double activeDelay = MPVCore::instance().getDouble("sub-delay");
    for (size_t i = 0; i < delays.size(); i++) {
        std::string number = fmt::format("{:+g}", delays[i]);
        std::replace(number.begin(), number.end(), '.', ',');
        names.push_back(delays[i] == 0 ? "hints/off"_i18n : brls::getStr("tsvitch/player/tracks/seconds", number));
        if (std::abs(delays[i] - activeDelay) < std::abs(delays[current] - activeDelay)) current = i;
    }
    btnSubDelay->init("tsvitch/player/tracks/sub_delay"_i18n, names, (int)current, [](int selected) {
        MPVCore::instance().command_async("set", "sub-delay", fmt::format("{}", delays[selected]));
    });
}

void PlayerSetting::setupCommonSetting() {
    auto locale = brls::Application::getLocale();

    btnMirror->init("tsvitch/player/setting/common/mirror"_i18n, MPVCore::VIDEO_MIRROR, [](bool value) {
        MPVCore::instance().setMirror(!MPVCore::VIDEO_MIRROR);
        GA("player_setting", {{"mirror", value ? "true" : "false"}});

        if (MPVCore::HARDWARE_DEC) {
            std::string hwdec = MPVCore::VIDEO_MIRROR ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD;
            MPVCore::instance().command_async("set", "hwdec", hwdec);
            brls::Logger::info("MPV hardware decode: {}", hwdec);
        }
    });

    btnSleep->setText("tsvitch/setting/app/playback/sleep"_i18n);
    updateCountdown(tsvitch::getUnixTime());
    btnSleep->registerClickAction([this](View* view) {
        std::vector<int> timeList           = {15, 30, 60, 90, 120};
        std::string min                     = "tsvitch/home/common/min"_i18n;
        std::vector<std::string> optionList = {"15 " + min, "30 " + min, "60 " + min, "90 " + min, "120 " + min};
        bool countdownStarted               = MPVCore::CLOSE_TIME != 0 && tsvitch::getUnixTime() < MPVCore::CLOSE_TIME;
        if (countdownStarted) {
            timeList.insert(timeList.begin(), -1);
            optionList.insert(optionList.begin(), "hints/off"_i18n);
        }
        BaseDropdown::text(
            "tsvitch/setting/app/playback/sleep"_i18n, optionList,
            [this, timeList, countdownStarted](int data) {
                if (countdownStarted && data == 0) {
                    MPVCore::CLOSE_TIME = 0;
                    GA("player_setting", {{"sleep", "-1"}});
                } else {
                    MPVCore::CLOSE_TIME = tsvitch::getUnixTime() + timeList[data] * 60;
                    GA("player_setting", {{"sleep", timeList[data]}});
                }
                updateCountdown(tsvitch::getUnixTime());
            },
            -1);
        return true;
    });

#ifdef ALLOW_FULLSCREEN
    auto& conf  = ProgramConfig::instance();
    btnFullscreen->init("tsvitch/setting/app/others/fullscreen"_i18n, conf.getBoolOption(SettingItem::FULLSCREEN),
                        [](bool value) {
                            ProgramConfig::instance().setSettingItem(SettingItem::FULLSCREEN, value);

                            VideoContext::FULLSCREEN = value;

                            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
                            GA("player_setting", {{"fullscreen", value ? "true" : "false"}});
                        });

    auto setOnTopCell = [this](bool enabled) {
        if (enabled) {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        } else {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        }
    };
    setOnTopCell(conf.getIntOptionIndex(SettingItem::ON_TOP_MODE) != 0);
    int onTopModeIndex = conf.getIntOption(SettingItem::ON_TOP_MODE);
    btnOnTopMode->setText("tsvitch/setting/app/others/always_on_top"_i18n);
    std::vector<std::string> onTopOptionList = {"hints/off"_i18n, "hints/on"_i18n,
                                                "tsvitch/player/setting/aspect/auto"_i18n};
    btnOnTopMode->setDetailText(onTopOptionList[onTopModeIndex]);
    btnOnTopMode->registerClickAction([this, onTopOptionList, setOnTopCell](brls::View* view) {
        BaseDropdown::text(
            "tsvitch/setting/app/others/always_on_top"_i18n, onTopOptionList,
            [this, onTopOptionList, setOnTopCell](int data) {
                btnOnTopMode->setDetailText(onTopOptionList[data]);
                ProgramConfig::instance().setSettingItem(SettingItem::ON_TOP_MODE, data);
                ProgramConfig::instance().checkOnTop();
                setOnTopCell(data != 0);
                GA("player_setting", {{"on_top_mode", data}});
            },
            ProgramConfig::instance().getIntOption(SettingItem::ON_TOP_MODE),
            "tsvitch/setting/app/others/always_on_top_hint"_i18n);
        return true;
    });

#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnOnTopMode->setVisibility(brls::Visibility::GONE);
#endif

    btnEqualizerReset->registerClickAction([this](View* view) {
        btnEqualizerBrightness->slider->setProgress(0.5f);
        btnEqualizerContrast->slider->setProgress(0.5f);
        btnEqualizerSaturation->slider->setProgress(0.5f);
        btnEqualizerGamma->slider->setProgress(0.5f);
        btnEqualizerHue->slider->setProgress(0.5f);
        return true;
    });
    registerHideBackground(btnEqualizerReset);

    setupEqualizerSetting(btnEqualizerBrightness, "tsvitch/player/setting/equalizer/brightness"_i18n,
                          SettingItem::PLAYER_BRIGHTNESS, MPVCore::instance().getBrightness());
    setupEqualizerSetting(btnEqualizerContrast, "tsvitch/player/setting/equalizer/contrast"_i18n,
                          SettingItem::PLAYER_CONTRAST, MPVCore::instance().getContrast());
    setupEqualizerSetting(btnEqualizerSaturation, "tsvitch/player/setting/equalizer/saturation"_i18n,
                          SettingItem::PLAYER_SATURATION, MPVCore::instance().getSaturation());
    setupEqualizerSetting(btnEqualizerGamma, "tsvitch/player/setting/equalizer/gamma"_i18n, SettingItem::PLAYER_GAMMA,
                          MPVCore::instance().getGamma());
    setupEqualizerSetting(btnEqualizerHue, "tsvitch/player/setting/equalizer/hue"_i18n, SettingItem::PLAYER_HUE,
                          MPVCore::instance().getHue());
}

void PlayerSetting::setupEqualizerSetting(brls::SliderCell* cell, const std::string& title, SettingItem item,
                                          int initValue) {
    if (initValue < -100) initValue = -100;
    if (initValue > 100) initValue = 100;
    cell->detail->setWidth(50);
    cell->title->setWidth(116);
    cell->title->setMarginRight(0);
    cell->slider->setStep(0.05f);
    cell->slider->setMarginRight(0);
    cell->slider->setPointerSize(20);
    cell->setDetailText(std::to_string(initValue));
    cell->init(title, (initValue + 100) * 0.005f, [cell, item](float value) {
        int data = (int)(value * 200 - 100);
        if (data < -100) data = -100;
        if (data > 100) data = 100;
        cell->detail->setText(std::to_string(data));
        switch (item) {
            case SettingItem::PLAYER_BRIGHTNESS:
                MPVCore::instance().setBrightness(data);
                break;
            case SettingItem::PLAYER_CONTRAST:
                MPVCore::instance().setContrast(data);
                break;
            case SettingItem::PLAYER_SATURATION:
                MPVCore::instance().setSaturation(data);
                break;
            case SettingItem::PLAYER_GAMMA:
                MPVCore::instance().setGamma(data);
                break;
            case SettingItem::PLAYER_HUE:
                MPVCore::instance().setHue(data);
                break;
            default:
                break;
        }
        static size_t iter = 0;
        brls::cancelDelay(iter);
        iter = brls::delay(200, []() {
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_BRIGHTNESS, MPVCore::VIDEO_BRIGHTNESS, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_CONTRAST, MPVCore::VIDEO_CONTRAST, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_SATURATION, MPVCore::VIDEO_SATURATION, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_GAMMA, MPVCore::VIDEO_GAMMA, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_HUE, MPVCore::VIDEO_HUE, false);
            ProgramConfig::instance().save();
        });
    });
    registerHideBackground(cell->getDefaultFocus());
}

void PlayerSetting::registerHideBackground(brls::View* view) {
    view->getFocusEvent()->subscribe([this](...) { this->setBackgroundColor(nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f)); });

    view->getFocusLostEvent()->subscribe(
        [this](...) { this->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop")); });
}

void PlayerSetting::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx) {
    static size_t updateTime = 0;
    size_t now               = tsvitch::getUnixTime();
    if (now != updateTime) {
        updateTime = now;
        updateCountdown(now);
    }
    Box::draw(vg, x, y, width, height, style, ctx);
}

void PlayerSetting::updateCountdown(size_t now) {
    if (MPVCore::CLOSE_TIME == 0 || now > MPVCore::CLOSE_TIME) {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        btnSleep->setDetailText("hints/off"_i18n);
    } else {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        btnSleep->setDetailText(tsvitch::sec2Time(MPVCore::CLOSE_TIME - now));
    }
}
