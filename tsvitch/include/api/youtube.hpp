#pragma once

#include <string>

namespace tsvitch::youtube {

/// A YouTube video for mpv: a master playlist that lists only the chosen picture and its sound, so FFmpeg's HLS
/// demuxer plays both (and does not open the ~20 variants of YouTube's own master first)
struct Stream {
    std::string playlist;
    int height = 0;
};

/// User agent and header mpv sends with the playlists and their segments
extern const char* const PLAYER_USER_AGENT;
extern const char* const PLAYER_HEADER;

/// The id of a YouTube video in a provider's field: the id itself, or a watch or youtu.be link; empty otherwise
std::string videoId(const std::string& text);

/**
 * Finds the streams of a YouTube video the way YTB Player does. YouTube's visionOS client gets an HLS master playlist
 * without a PO token: first a visitor id (kept for 30 minutes), then the player request, then the master playlist.
 * The picture is the H.264 variant up to maxHeight, the sound the video's original track (never an automatic dub);
 * the playlist lists the variant first, as FFmpeg recognises HLS by the first bytes it reads.
 *
 * Blocking: three requests, so call it off the UI thread. False with a short reason when the video cannot play
 * (removed, private, age restricted...).
 */
bool resolve(const std::string& id, int maxHeight, Stream& stream, std::string& error);

/// The app closes: a request on its way stops
void stopRequests();

}  // namespace tsvitch::youtube
