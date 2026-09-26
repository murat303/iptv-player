#pragma once

#include "api/tsvitch/result/home_live_result.h"

namespace tsvitch {

/// Downloads a movie or an episode to the SD card (live channels and series cannot be downloaded)
void startVideoDownload(const tsvitch::LiveM3u8& channel);

}  // namespace tsvitch
