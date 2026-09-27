#pragma once
#include <string>
#include <borealis/core/event.hpp> // aggiungi questa riga
#include "api/tsvitch/result/home_live_result.h" // aggiungi questa riga

/// The items of the same kind as items[index], without series pages (they cannot play): the player's
/// next/previous buttons move between them. start: where items[index] is in the result.
std::vector<tsvitch::LiveM3u8> sameKindPlaylist(const std::vector<tsvitch::LiveM3u8>& items, size_t index,
                                                size_t& start);

class Intent {
public:

    // seriesPlaylist: the list holds the episodes of a series in order (the next one can start by itself)
    static void openLive(const std::vector<tsvitch::LiveM3u8>& channelList, size_t index, std::function<void()> onClose,
                         bool seriesPlaylist = false);

    // Information screen of an Xtream movie or series (series items carry the xtream-series:// url)
    // onClose: the screen was left (the list shows what was marked there)
    static void openXtreamDetail(const tsvitch::LiveM3u8& item, std::function<void()> onClose = nullptr);
    // A collection of the discovery screen (genre, theme, studio, award...) as a poster grid
    static void openDiscoverList(const std::string& collectionId, const std::string& title,
                                 std::function<void()> onClose = nullptr);

    // True once the app is closing: borealis then deletes the bottom screen first, so the close callbacks of the
    // screens above it must not touch the lists below
    static bool isClosing();

    static void openPgcFilter(const std::string& filter);

    static void openSettings(std::function<void()> onClose = nullptr);

    static void openInbox();

    static void openHint();

    static void openMain();

    static void openGallery(const std::vector<std::string>& data);

    static void openDLNA();

    static void openSearch(const std::string& key);

    static void openActivity(const std::string& id);

    static void _registerFullscreen(brls::Activity* activity);
};

#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
#define ALLOW_FULLSCREEN
#endif

#ifdef ALLOW_FULLSCREEN
#define registerFullscreen(activity) Intent::_registerFullscreen(activity)
#else
#define registerFullscreen(activity) (void)activity
#endif

// Struttura per i dati Xtream
struct XtreamData {
    std::string url;
    std::string username;
    std::string password;
};

// Evento globale per notificare il cambio M3U8
inline brls::Event<> OnM3U8UrlChanged;

// Evento globale per notificare il cambio Proxy
inline brls::Event<> OnProxyUrlChanged;
// Evento globale per notificare il cambio modalità IPTV
inline brls::Event<> OnIPTVModeChanged;
// Evento globale per notificare il cambio Xtream
inline brls::Event<XtreamData> OnXtreamChanged;
