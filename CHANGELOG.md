# Changelog

## 1.3.1 - 2026-09-29

- Favorites: movies and series show as posters like in the lists, channels as channel cards. With favorites of
  several kinds a chip per kind (Live TV, Movies, Series) picks what is shown; L and R change it.
- A series is a favorite, not its episodes: the heart of the player and X in the history add the series of an
  episode. Episodes added by an earlier version turn into their series.

## 1.3.0 - 2026-09-29

### Trailers
- The detail screen of a movie or a series has a Trailer button when TMDB or the provider knows a trailer. It plays
  from YouTube in the app's own player: TMDB's trailers in the app's language first, then in English, then the one
  the provider lists. When YouTube refuses one (removed, private...), the next one plays.
- 1080p on the TV, 720p in handheld mode or without hardware decoding. The player closes at the end of the trailer;
  a trailer does not go to the history and changes no watched marks or playback positions.

### Player
- Playback speed in the player settings, like YouTube: 0.25x to 2x. It stays for the next episode while the player
  is open; a movie or an episode opened again starts at normal speed. Live channels have no speed. At 1.5x and 2x
  a 1080p video can drop some frames: the Switch draws about 33 frames a second in this player.

### Detail screen
- A plot that does not fit ends with "Read more". The plot, director and cast box then takes the focus, and A (or a
  tap) shows all of it in a window; a long text scrolls with up and down.

## 1.2.0 - 2026-09-28

### Discover
- A new Discover tab: the title of the week, trending this week, recommendations after the watched titles, new
  movies and series.
- Genre tiles for movies and series, and collections as cards with their posters: box office hits, all-time classics,
  based on books, mind twists, true stories, epic fantasy, superheroes, time travel and more.
- Studios and platforms (Pixar, Disney, Marvel, Netflix, HBO... and the TV channels of the provider's series), world
  cinema (Turkish, Korean, anime...), award winners (Oscar, Cannes, Venice, Berlin, Emmy), decades and film series.
- Only titles of the provider's lists are shown, without adult content and categories locked by the PIN; a movie that
  is in several categories shows once.
- Settings → Discover: TMDB on or off, how far its data is, the shelves shown, and forgetting the data.
- Episodes watched with an earlier version count for the recommendations too: their series is found by the name of
  the episode ("Name - S01E03 - ...").

### Genres
- The movie and series lists have a "Genres" group: a tile per genre opens all of its titles.

### Detail screen
- Movies show TMDB's rating with its number of votes and similar titles of the catalogue (the other movies of its
  film series first). Series show TMDB's rating. The plot, the director and the cast come from TMDB when the
  provider does not list them; the plot in the app's language, or in English when TMDB has none in it.

### Fixes
- The language names in Settings showed broken letters.
- The pictures of the guide to open the app through a game (and to add a HOME menu shortcut) show IPTV Player.
- A card could show the picture of another title when a list opened while many pictures were loading (for example
  the history right after the Discover tab): a picture that arrives late no longer replaces the newer one.

### How it works
- The provider's lists already carry the TMDB id of most titles: each title is asked about once at TMDB
  (themoviedb.org) in the background, about 30 a second (a catalogue of 15,000 titles in about eight minutes the
  first time), and kept on the SD card. It waits while a video plays and goes on where it stopped when the app
  starts again.
- Lists saved by an earlier version are downloaded again once, with the next automatic refresh.

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
