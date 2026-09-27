# Changelog

## 1.1.2 - 2026-09-27

- A subtitle picked by hand stays picked also when another subtitle has the same language, such as a forced Turkish one next to the full Turkish one: the title and the forced flag tell them apart. Before, the player went back to the forced one when a video was opened again.

## 1.1.1 - 2026-09-27

- An audio or subtitle track picked by hand stays picked in the next episode also when the track has no language: it is found again by its title, or within the same series by its place in the list. Before, the next episode went back to the file's default track.
- A movie or an episode that counts as watched (90%) but was left before its end resumes where it was left. Only its credits (where the next episode is offered), its last 30 seconds or a mark by hand start it from the beginning.

## 1.1.0 - 2026-09-27

### Watched marks
- Movies and episodes count as watched after 90% of them was played (as in Plex, Jellyfin and Kodi), or when the next episode is offered at the credits; their cards show a check.
- Y marks a movie or an episode as watched or not in the detail screens: on a series, the focused episode, or the episode of the play button.
- Bars under the pictures show how far a movie or an episode was played: in the lists, the movie details, the episodes, favorites and history.
- The play button of a series skips watched episodes; after the last episode of a season it offers the next season's first one.
- Episodes watched to the end with an earlier version start as watched.

### Next episode
- The next episode is offered at the closing credits: at a chapter named for them when the video has chapters, otherwise 30 s, 1 min (the default) or 2 min before the end, or when the episode ends (Settings > Playback).
- Cancel keeps the credits playing; when the episode ends the next one is offered again.

### Connection test
- Settings > IPTV > Connection test asks the provider three small things one after the other (the account, the series categories, the series opened last) and shows how long each took and how many tries it needed.
- The same screen lists the app's last 10 requests with their times, tries and how long they waited in the app, so a slow screen can be traced to the provider or to the app.

### Other
- The L and R glyphs sit beside the season buttons, so the footer has room for Y.
- Closing the app while a video or a detail screen is open no longer touches the lists behind them.

## 1.0.0 - 2026-09-26

First release of IPTV Player: a modified version of TsVitch 0.3.2 with the Xtream movies and series work of ratk.
Changes by muratgokce:

### Movies and series
- Detail pages for movies and series: poster, backdrop, plot, cast, director, rating, duration, and resume or start over.
- Seasons and episodes with stills and progress; L and R change the season.
- The next episode starts after a 10-second countdown (can be turned off in the settings).
- Poster grids with ratings and years, a "Recently added" group, sorting (server order, recently added, rating, name, year) and search that ignores case and accents.
- The lists of the provider are kept on the SD card and refreshed in the background every day, every week or only with the refresh button (a setting); busy providers are retried with pauses.
- While a list comes from the provider, a card shows what is happening: waiting, downloading with a progress bar, retries of a busy provider and preparing. When the provider does not tell the size, the bar uses the size of the last download.
- Back leaves a download running; opening the list again shows its card again. The home screen shows the state of each list and the number of channels, movies and series.

### Live TV
- Now / next programme guide (EPG) on the player, refreshed when a programme ends.
- The progress bar no longer runs on live channels.
- Channel cards no longer show running numbers.

### Player
- Audio and subtitle track pickers; the choice is remembered.
- Subtitle size, color, background, position and delay. Subtitles show on the Switch without extra fonts.
- The D-pad skips 10 seconds back or forward; A pauses and resumes.
- L and R change the channel on live TV only; on movies and episodes they do nothing, and the end of a channel list no longer closes the player.
- Subtitles move above the player controls while the controls are on screen.
- Playback resumes where it stopped; the old picture no longer shows while the next video loads.

### Downloads
- New download manager: one download at a time, pause and continue, covers, and playback without internet.

### Other
- History: remove one video or clear everything.
- Account info: status, end date, connections, and a reminder before the subscription ends.
- Own name and data folder (`/switch/iptv-player`); the settings, favorites and history of TsVitch are copied at the first start, and the app says so.
- The home screen no longer shows the buttons of the last list (search, favorite, download, refresh, sort).
- Turkish translation; Italian and Brazilian Portuguese completed; English is the default language.
- The password is hidden in the settings.
- No analytics, no ads server and no update check.
- Fixes: crashes on exit and during downloads, closing the app returns to the launcher, no freeze when the provider does not answer, many focus fixes.

## Earlier versions

IPTV Player 1.0.0 is based on TsVitch 0.3.2. The history of TsVitch is in its repository:
https://github.com/giovannimirulla/TsVitch
