#pragma once

#include <memory>
#include <string>
#include <vector>

#include <borealis/core/activity.hpp>

#include "api/tmdb.hpp"

class VideoView;

namespace tsvitch::youtube {
struct Stream;
}

/// A trailer from YouTube in the app's player. The videos are tried in order until YouTube lets one play, and the
/// player closes at the end of it. Nothing goes to the history, the playback positions or the watched marks.
class TrailerActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/video_activity.xml");

    TrailerActivity(std::string title, std::vector<tsvitch::TmdbVideo> videos);

    ~TrailerActivity() override;

    void onContentAvailable() override;

private:
    // Asks YouTube for videos[index] off the UI thread, then plays it or goes on with the next one
    void resolve(size_t index);

    void play(const tsvitch::youtube::Stream& stream, const tsvitch::TmdbVideo& trailer);

    void close();

    VideoView* video = nullptr;
    std::string title;
    std::vector<tsvitch::TmdbVideo> videos;
    // False once the screen is gone: YouTube's late answers check it
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool closing                = false;
};
