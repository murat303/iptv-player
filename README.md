<p align="center">
  <img src="resources/icon/icon.png" alt="IPTV Player" width="128" height="128"/>
</p>
<h1 align="center">IPTV Player</h1>
<p align="center">Live TV, movies and series from your own IPTV subscription, on a Nintendo Switch running homebrew.</p>

<p align="center">
  <img src="docs/screenshots/discover.jpg" width="49%" alt="Discover"/>
  <img src="docs/screenshots/live-player.jpg" width="49%" alt="Live TV with the programme guide"/>
</p>

IPTV Player plays the channels, movies and series of an IPTV service that you already have. It works with
**Xtream Codes API** accounts and **M3U / M3U8** playlists. It does not come with any channels, playlists or accounts.

## Features

- **Live TV:** categories, channel logos, favorites, a now / next programme guide in the player, L and R to change the channel.
- **Movies and series:** poster grids with ratings and years, watched marks and progress bars, sorting (recently added, rating, name, year), search, and detail pages with the plot, cast, backdrop and a resume button.
- **Discover:** a tab with the title of the week, continue watching, this week's trending titles, recommendations
  after what you watched, new movies and series, genre tiles, collections (box office hits, all-time classics, based on
  books, mind twists...), studios and platforms, world cinema, award winners (Oscar, Cannes, Venice, Berlin, Emmy),
  decades and film series. It shows only what your provider's lists have; shelves can be hidden in the settings.
- **Genres:** movies and series by genre (Action, Comedy, Drama...) in the Discover tab and in the lists.
- **Series:** seasons and episodes with stills and progress, L and R to change the season, and the next episode offered at the closing credits.
- **Player:** audio and subtitle tracks, subtitle size / color / background / position / delay, 10-second skips with the D-pad, and playback resumes where you left off.
- **Watched:** movies and episodes count as watched after 90% of them was played, like in Plex, Jellyfin and Kodi.
  Y marks them by hand in the detail screens, and the play button of a series goes on with the first unwatched episode.
- **Downloads:** save movies and episodes to the SD card, pause and continue them, and watch them without internet.
- **Saved lists:** the lists of the provider are kept on the SD card and open at once. They are refreshed in the
  background every day, every week or only with the refresh button, and a card shows how the download goes.
- **History and favorites**, a PIN lock for adult categories, the account status (end date, connections) and a connection test that times the provider's answers.
- **Languages:** English, Turkish, Italian and Brazilian Portuguese.

## Privacy

No analytics, no ads, no sign-up. The server address, username and password are saved only on your SD card and
are sent only to your provider. Posters and logos load from the addresses in your provider's lists.

The analytics (Google Analytics), the user registration and ad server and the update check of the original code are
turned off when the app is built (`DISABLE_ANALYTICS`, `DISABLE_UPDATE_CHECK` and no `SERVER_URL` in
[CMakeLists.txt](CMakeLists.txt)).

The Discover tab and the genres ask [TMDB](https://www.themoviedb.org) about the titles of your lists by their TMDB
ids, and for the recommendations about titles you watched. Your account, your lists and your history are never sent;
the answers are kept on the SD card. Settings → Discover turns it off.

## Screenshots

<table>
  <tr>
    <td><img src="docs/screenshots/home.jpg" alt="Home"/><br/>Home</td>
    <td><img src="docs/screenshots/live-tv.jpg" alt="Live TV"/><br/>Live TV</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/discover.jpg" alt="Discover"/><br/>Discover</td>
    <td><img src="docs/screenshots/discover-genres.jpg" alt="Genres and collections"/><br/>Genres and collections</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/collection.jpg" alt="A collection"/><br/>A collection</td>
    <td><img src="docs/screenshots/genres.jpg" alt="Genres in the movie list"/><br/>Genres in the movie list</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/movies.jpg" alt="Movies"/><br/>Movies</td>
    <td><img src="docs/screenshots/movie-detail.jpg" alt="Movie details"/><br/>Movie details</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/movie-detail-tmdb.jpg" alt="Rating, cast and similar titles from TMDB"/><br/>Rating, cast and similar titles from TMDB</td>
    <td><img src="docs/screenshots/settings-discover.jpg" alt="Discover settings"/><br/>Discover settings</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/series-detail.jpg" alt="Seasons and episodes"/><br/>Seasons and episodes</td>
    <td><img src="docs/screenshots/player.jpg" alt="Player"/><br/>Player</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/player-settings.jpg" alt="Audio and subtitles"/><br/>Audio and subtitles</td>
    <td><img src="docs/screenshots/live-player.jpg" alt="Live TV with the programme guide"/><br/>Live TV with the programme guide</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/next-episode.jpg" alt="Next episode"/><br/>Next episode</td>
    <td><img src="docs/screenshots/search.jpg" alt="Search"/><br/>Search</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/favorites.jpg" alt="Favorites"/><br/>Favorites</td>
    <td><img src="docs/screenshots/downloads.jpg" alt="Downloads"/><br/>Downloads</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/history.jpg" alt="History"/><br/>History</td>
    <td><img src="docs/screenshots/settings.jpg" alt="IPTV settings"/><br/>IPTV settings</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/account.jpg" alt="Account info"/><br/>Account info</td>
    <td><img src="docs/screenshots/about.jpg" alt="About"/><br/>About</td>
  </tr>
</table>

The screenshots show the open movies of the Blender Foundation: *Big Buck Bunny*, *Sintel*, *Tears of Steel*,
*Spring*, *Cosmos Laundromat*, *Sprite Fright*, *Elephants Dream*, *Charge* and *Caminandes*
(© Blender Foundation / Blender Studio, [studio.blender.org](https://studio.blender.org), licensed
[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) and [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/);
posters from Wikimedia Commons, resized, and the Caminandes cover made from an episode picture).
The channel names and logos are made up, and the ratings are sample values. The Discover pictures add the made-up
titles of an earlier showcase, and their TMDB data (genres, votes, people, similar titles) is made up too: the
people's pictures are their initials. The top of the Discover tab shows a frame of *Sintel*.

## Installation

1. Download `iptv-player.nro` from the [latest release](https://github.com/murat303/iptv-player/releases/latest).
2. Copy it to the SD card as `/switch/iptv-player/iptv-player.nro`.
3. Start it from the Homebrew Menu or a launcher such as Sphaira.

Starting the Homebrew Menu through a game (hold **R** while you open a game) gives the app more memory, which helps
with very large lists.

## First start

1. Open **Settings → IPTV**.
2. Pick the **IPTV mode**: *Xtream Codes* or *M3U8*.
3. For Xtream Codes, enter the server address (for example `http://server.com:8080`), the username and the password.
   For M3U8, enter the playlist address.
4. Go back to **Home** and open Live TV, Movies or Series.

The app is in English by default. The language can be changed in **Settings → UI → Language**.

## Controls

| Button | Lists | Details | Player |
|---|---|---|---|
| A | Open | Play | Play / pause |
| B | Back | Back | Back |
| X | Favorite | Favorite | |
| Y | Search | Mark as watched | Show / hide controls |
| L | Sort | Previous season | Previous channel |
| R | Refresh | Next season | Next channel |
| ZR | Download | Download | Volume with ↑ / ↓ |
| ← / → | Move | Move | Skip 10 s back / forward |
| − | | | Video information |

In the history, **Y** removes the selected video. The player controls can also be used by touch.

## Files

Everything the app saves is in `/switch/iptv-player/`, next to the app: the settings, favorites, watch history,
playback positions, the cached lists of the provider, the TMDB data of the Discover tab (`tmdb/`) and the downloads.

When the app starts for the first time, it copies the settings, favorites and history of
[TsVitch](https://github.com/giovannimirulla/TsVitch) from `/config/tsvitch` if they exist. It only copies them:
TsVitch keeps its own files. Videos downloaded with TsVitch stay in its folder and still play from there.

## Building

The Nintendo Switch build needs [Docker](https://www.docker.com/):

```bash
git clone --recursive https://github.com/murat303/iptv-player
cd iptv-player
docker run --rm -v "$PWD:/data" devkitpro/devkita64 bash /data/scripts/build_switch.sh
```

The result is `cmake-build-switch/iptv-player.nro`. Every push is also built by GitHub Actions.

The Discover tab and the genres need a [TMDB API key](https://developer.themoviedb.org/docs/getting-started):
set `TMDB_API_KEY` for the build script (`docker run -e TMDB_API_KEY=...`). Without it they show only what the
provider's lists tell. A key in `/switch/iptv-player/tmdb_key.txt` is used instead of the built-in one.

Only the Nintendo Switch version is maintained. The files for the other platforms come from TsVitch and are not tested.

## Credits

IPTV Player is a modified version of **[TsVitch](https://github.com/giovannimirulla/TsVitch)** by giovannimirulla,
with the Xtream movies and series support of **[ratk](https://github.com/ratk/TsVitch)**. TsVitch is based on
**[wiliwili](https://github.com/xfangfang/wiliwili)** by xfangfang.

It is built with [borealis](https://github.com/xfangfang/borealis), [mpv](https://mpv.io),
[FFmpeg](https://ffmpeg.org), [libass](https://github.com/libass/libass), [cpr](https://github.com/libcpr/cpr),
[nlohmann/json](https://github.com/nlohmann/json) and [lunasvg](https://github.com/sammycage/lunasvg).

Movie and series data for the Discover tab: [TMDB](https://www.themoviedb.org). This product uses the TMDB API but is
not endorsed or certified by TMDB. The award winners come from [Wikidata](https://www.wikidata.org) (CC0).

The changes of IPTV Player (2026, muratgokce) are listed in the [changelog](CHANGELOG.md).

## License

IPTV Player is free software under the [GNU General Public License v3.0](LICENSE), like TsVitch.
It comes with no warranty.

## Disclaimer

IPTV Player is only a player. It does not provide, host or link to any content. Use it only with services and
content that you are allowed to watch. IPTV Player is not affiliated with Nintendo or with any IPTV provider.

---

## Türkçe

IPTV Player, Nintendo Switch'te (homebrew) kendi IPTV aboneliğinin canlı kanallarını, filmlerini ve dizilerini
izlemeni sağlar. Xtream Codes hesaplarıyla ve M3U / M3U8 listeleriyle çalışır; içinde hiçbir kanal, liste ya da hesap yoktur.

**Kurulum:** [Son sürümden](https://github.com/murat303/iptv-player/releases/latest) `iptv-player.nro` dosyasını indir ve
SD kartta `/switch/iptv-player/` klasörüne kopyala. Homebrew menüsünden ya da Sphaira'dan aç.

**İlk açılış:** Ayarlar → IPTV bölümünden Xtream Codes ya da M3U8 seç ve hesap bilgilerini gir.
Arayüzü Türkçe yapmak için: Settings → UI → Language → Türkçe.

**İzlendi:** %90'ı izlenen film ve bölümler izlendi sayılır. Detay ekranlarında Y ile elle de işaretleyebilirsin.
Sonraki bölüm, videoda jenerik işareti varsa orada, yoksa ayarlardan seçtiğin sürede (varsayılan: bitmeden 1 dk önce) sorulur.

**Keşfet:** Haftanın yapımı, izlemeye devam et, bu hafta trend olanlar, izlediklerine göre öneriler, yeni filmler ve diziler,
türler, koleksiyonlar (gişe rekortmenleri, klasikler, kitaptan uyarlamalar, akıl oyunları...), stüdyolar ve
platformlar, dünya sineması, ödüllü yapımlar, on yıllar ve film serileri. Yalnız sağlayıcının listelerinde olanlar
görünür. Film ve Dizi listelerinde de "Türler" grubu var. Satırlar Ayarlar → Keşfet bölümünden gizlenebilir.

**Bağlantı testi:** Bir ekran yavaş açılıyorsa Ayarlar → IPTV → Bağlantı testi, sağlayıcının cevap sürelerini ve uygulamanın son isteklerini gösterir.

**Listeler:** Sağlayıcının listeleri SD karta kaydedilir ve hemen açılır. Arka planda her gün yenilenir;
bunu Ayarlar → IPTV bölümünden haftada bire çekebilir ya da kapatabilirsin.

**Gizlilik:** Analytics, reklam ve üyelik yok. Sunucu adresin, kullanıcı adın ve şifren yalnız SD kartında saklanır
ve yalnız kendi sağlayıcına gönderilir. Keşfet sekmesi TMDB'ye yalnız yapımların TMDB kimliklerini sorar (öneriler için
izlediklerininkini de); hesabın, listelerin ve geçmişin gönderilmez. Ayarlar → Keşfet bölümünden kapatılabilir.

TsVitch'i kullandıysan ayarların, favorilerin ve geçmişin ilk açılışta kendiliğinden gelir.
