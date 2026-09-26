#include "core/DownloadManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sys/stat.h>
#include <curl/curl.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>

#include "utils/config_helper.hpp"
#include "utils/image_helper.hpp"

using json = nlohmann::json;
using namespace std::chrono_literals;

/// State of one HTTP request of a download, shared with the curl callbacks
struct DownloadTransfer {
    DownloadManager* manager = nullptr;
    CURL* curl               = nullptr;
    std::FILE* file          = nullptr;
    std::string id;
    std::string path;
    size_t base        = 0;  // bytes in the file before this request
    size_t written     = 0;  // bytes written by this request
    size_t total       = 0;  // size of the whole video, 0 while unknown
    bool started       = false;  // the first bytes of the answer arrived
    bool writeError    = false;
    bool notVideo      = false;  // the server answered with a text page
    size_t speedBytes  = 0;
    double speed       = 0;
    std::chrono::steady_clock::time_point lastUpdate, lastSave, speedTime;
};

namespace {

// Seconds to wait after the n-th network error in a row; the download fails after the last one
constexpr int RETRY_DELAYS[] = {3, 6, 12, 20, 30, 30};
constexpr int MAX_FAILURES   = sizeof(RETRY_DELAYS) / sizeof(RETRY_DELAYS[0]);

bool isRunning(DownloadStatus status) {
    return status == DownloadStatus::DOWNLOADING || status == DownloadStatus::PENDING;
}

std::string trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

// File name without the characters the SD card file system refuses
std::string safeFileName(const std::string& title) {
    std::string name;
    for (char c : title) name += (static_cast<unsigned char>(c) < 32 || std::strchr("\\/:*?\"<>|", c)) ? '_' : c;
    if (name.size() > 120) {
        size_t cut = 120;  // on a UTF-8 character boundary
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) cut--;
        name.resize(cut);
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? "video" : name;
}

// IPTV movie urls end with the container extension (.mkv, .mp4...)
std::string extensionFromUrl(const std::string& url) {
    std::string path = url.substr(0, url.find_first_of("?#"));
    size_t dot       = path.find_last_of('.');
    size_t slash     = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        for (const char* known : {".mkv", ".mp4", ".avi", ".ts", ".m4v", ".mov", ".webm", ".flv", ".mpg", ".wmv"})
            if (ext == known) return ext;
    }
    return ".mp4";
}

size_t fileSize(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 ? static_cast<size_t>(st.st_size) : 0;
}

void removeFile(const std::string& path) {
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

float percent(size_t done, size_t total) {
    return total ? static_cast<float>(std::min(100.0, static_cast<double>(done) * 100.0 / static_cast<double>(total)))
                 : 0.0f;
}

// The console must not fall asleep in the middle of a download
void keepConsoleAwake(bool awake) {
    brls::sync([awake]() { brls::Application::getPlatform()->disableScreenDimming(awake, "Downloading", "IPTV"); });
}

size_t transferWrite(char* data, size_t size, size_t count, void* userData) {
    auto* t      = static_cast<DownloadTransfer*>(userData);
    size_t bytes = size * count;
    if (!t->started) {
        t->started = true;
        long code  = 0;
        curl_easy_getinfo(t->curl, CURLINFO_RESPONSE_CODE, &code);
        // Some panels answer an expired or refused request with a text page instead of the video
        char* type = nullptr;
        curl_easy_getinfo(t->curl, CURLINFO_CONTENT_TYPE, &type);
        bool textType = type && (std::strncmp(type, "text/", 5) == 0 || std::strstr(type, "json"));
        if (code != 206 && textType && bytes > 0 && (data[0] == '<' || data[0] == '{')) {
            t->notVideo = true;
            return 0;
        }
        if (t->base > 0 && code != 206) {
            // The server ignored the range and sends the whole video: the file starts again
            std::fclose(t->file);
            t->file = std::fopen(t->path.c_str(), "wb");
            t->base = 0;
            t->speedBytes = 0;
            if (!t->file) {
                t->writeError = true;
                return 0;
            }
        }
        curl_off_t length = -1;
        curl_easy_getinfo(t->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
        if (length > 0) t->total = t->base + static_cast<size_t>(length);
    }
    if (std::fwrite(data, 1, bytes, t->file) != bytes) {
        t->writeError = true;
        return 0;
    }
    t->written += bytes;
    return bytes;
}

int transferProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* t = static_cast<DownloadTransfer*>(userData);
    return t->manager->onTransferProgress(*t) ? 0 : 1;
}

}  // namespace

DownloadManager::DownloadManager() {
    exitSubscription = brls::Application::getExitEvent()->subscribe([this]() { shutdown(); });
}

DownloadManager::~DownloadManager() {
    brls::Application::getExitEvent()->unsubscribe(exitSubscription);
    shutdown();
}

void DownloadManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        bool changed = false;
        for (auto& item : downloads) {
            if (!isRunning(item.status)) continue;
            item.status = DownloadStatus::PAUSED;
            changed     = true;
        }
        if (changed) saveLocked();
    }
    shouldStop = true;
    if (!worker.joinable()) return;
    // curl notices within a second; closing the app must not wait much longer
    for (int i = 0; i < 40; i++) {
        {
            std::lock_guard<std::mutex> lock(downloadsMutex);
            if (!workerBusy) break;
        }
        std::this_thread::sleep_for(50ms);
    }
    bool busy;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        busy = workerBusy;
    }
    if (busy)
        worker.detach();
    else
        worker.join();
}

std::string DownloadManager::startDownload(const std::string& title, const std::string& url,
                                           const std::string& imageUrl) {
    std::string cleanUrl = trim(url);
    if (cleanUrl.rfind("http://", 0) != 0 && cleanUrl.rfind("https://", 0) != 0) return "";
    loadDownloads();

    std::string id;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        for (auto& item : downloads) {
            if (item.url != cleanUrl) continue;
            id = item.id;
            bool fileGone = item.status == DownloadStatus::COMPLETED && fileSize(item.localPath) == 0;
            if (item.status == DownloadStatus::PAUSED || item.status == DownloadStatus::FAILED ||
                item.status == DownloadStatus::CANCELLED || fileGone) {
                if (fileGone) {
                    item.progress       = 0;
                    item.downloadedSize = 0;
                }
                item.status = DownloadStatus::PENDING;
                item.error.clear();
                version++;
                saveLocked();
            }
            break;
        }
        if (id.empty()) {
            DownloadItem item;
            item.id        = generateDownloadId();
            item.title     = title.empty() ? "Video" : title;
            item.url       = cleanUrl;
            item.imageUrl  = imageUrl;
            item.localPath = getDownloadDirectory() + "/" + safeFileName(item.title) + "_" + item.id +
                             extensionFromUrl(cleanUrl);
            item.status    = DownloadStatus::PENDING;
            id             = item.id;
            downloads.push_back(std::move(item));
            version++;
            saveLocked();
        }
    }
    runQueue();
    return id;
}

void DownloadManager::pauseDownload(const std::string& id) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto it = findDownload(id);
    if (it == downloads.end() || !isRunning(it->status)) return;
    it->status = DownloadStatus::PAUSED;
    it->speed  = 0;
    version++;
    saveLocked();
}

void DownloadManager::resumeDownload(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it == downloads.end()) return;
        if (it->status != DownloadStatus::PAUSED && it->status != DownloadStatus::FAILED &&
            it->status != DownloadStatus::CANCELLED)
            return;
        it->status = DownloadStatus::PENDING;
        it->error.clear();
        version++;
        saveLocked();
    }
    runQueue();
}

void DownloadManager::deleteDownload(const std::string& id) {
    std::string path, image;
    bool running;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it == downloads.end()) return;
        path    = it->localPath;
        image   = it->imagePath;
        running = it->status == DownloadStatus::DOWNLOADING;
        downloads.erase(it);
        version++;
        saveLocked();
    }
    removeFile(image);
    // The worker still has the file of a running download open: it removes the file when it stops
    if (!running) removeFile(path);
    brls::Logger::info("DownloadManager: deleted {}", id);
}

std::vector<DownloadItem> DownloadManager::getAllDownloads() const {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    return downloads;
}

DownloadItem DownloadManager::getDownload(const std::string& id) const {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto it = findDownload(id);
    return it != downloads.end() ? *it : DownloadItem{};
}

bool DownloadManager::getActiveDownload(DownloadItem& item) const {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    const DownloadItem* waiting = nullptr;
    for (const auto& d : downloads) {
        if (d.status == DownloadStatus::DOWNLOADING) {
            item = d;
            return true;
        }
        if (d.status == DownloadStatus::PENDING && !waiting) waiting = &d;
    }
    if (!waiting) return false;
    item = *waiting;
    return true;
}

bool DownloadManager::findByUrl(const std::string& url, DownloadItem& item) const {
    std::string cleanUrl = trim(url);
    std::lock_guard<std::mutex> lock(downloadsMutex);
    for (const auto& d : downloads) {
        if (d.url != cleanUrl) continue;
        item = d;
        return true;
    }
    return false;
}

std::string DownloadManager::getDownloadDirectory() const {
    return ProgramConfig::instance().getConfigDir() + "/downloads";
}

void DownloadManager::runQueue() {
    std::string next;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        if (workerBusy || shouldStop) return;
        for (const auto& item : downloads) {
            if (item.status != DownloadStatus::PENDING) continue;
            next = item.id;
            break;
        }
        if (next.empty()) return;
        workerBusy = true;
    }
    // The previous worker already left its loop (workerBusy was false): joining it takes no time
    if (worker.joinable()) worker.join();
    keepConsoleAwake(true);
    worker = std::thread(&DownloadManager::workerLoop, this, next);
}

void DownloadManager::workerLoop(std::string id) {
    while (!id.empty()) {
        runDownload(id);
        std::lock_guard<std::mutex> lock(downloadsMutex);
        id.clear();
        if (!shouldStop) {
            for (const auto& item : downloads) {
                if (item.status != DownloadStatus::PENDING) continue;
                id = item.id;
                break;
            }
        }
        if (id.empty()) workerBusy = false;
    }
    keepConsoleAwake(false);
}

bool DownloadManager::stillDownloading(const std::string& id) const {
    if (shouldStop) return false;
    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto it = findDownload(id);
    return it != downloads.end() && it->status == DownloadStatus::DOWNLOADING;
}

void DownloadManager::runDownload(const std::string& id) {
    std::string url, path;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it == downloads.end() || it->status != DownloadStatus::PENDING) return;
        it->status = DownloadStatus::DOWNLOADING;
        it->error.clear();
        url  = it->url;
        path = it->localPath;
        version++;
        saveLocked();
    }
    brls::Logger::info("DownloadManager: downloading {}", id);
    fetchCover(id);

    int failures = 0;
    std::string error;
    for (;;) {
        bool madeProgress     = false;
        TransferResult result = transferOnce(id, url, path, error, madeProgress);
        if (result == TransferResult::DONE) {
            finishDownload(id, DownloadStatus::COMPLETED, "");
            break;
        }
        if (result == TransferResult::STOPPED) break;
        if (madeProgress) failures = 0;
        if (result == TransferResult::FATAL_ERROR || failures >= MAX_FAILURES) {
            finishDownload(id, DownloadStatus::FAILED, error);
            break;
        }
        int delay = RETRY_DELAYS[failures++];
        brls::Logger::warning("DownloadManager: {} interrupted ({}), trying again in {} s", id, error, delay);
        // Wait, unless the download is paused or deleted meanwhile
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
        bool keep  = true;
        while (keep && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(250ms);
            keep = stillDownloading(id);
        }
        if (!keep) break;
    }

    // A download deleted while its file was open: the file can go now
    bool deleted;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        deleted = findDownload(id) == downloads.end();
    }
    if (deleted) removeFile(path);
}

void DownloadManager::fetchCover(const std::string& id) {
    std::string imageUrl, imagePath;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it == downloads.end() || (!it->imagePath.empty() && fileSize(it->imagePath) > 0)) return;
        imageUrl  = ImageHelper::smallPoster(it->imageUrl);
        imagePath = getDownloadDirectory() + "/" + id + "_cover.jpg";
    }
    if (imageUrl.rfind("http", 0) != 0) return;

    std::error_code ec;
    std::filesystem::create_directories(getDownloadDirectory(), ec);
    std::string temp = imagePath + ".part";
    std::FILE* file  = std::fopen(temp.c_str(), "wb");
    if (!file) return;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        removeFile(temp);
        return;
    }
    curl_easy_setopt(curl, CURLOPT_URL, imageUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TsVitch/1.0");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t count, void* out) {
        return std::fwrite(data, size, count, static_cast<std::FILE*>(out)) * size;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    std::fclose(file);
    if (res != CURLE_OK || fileSize(temp) < 512) {
        removeFile(temp);
        return;
    }
    removeFile(imagePath);
    std::filesystem::rename(temp, imagePath, ec);
    if (ec) return;

    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto it = findDownload(id);
    if (it == downloads.end()) {
        removeFile(imagePath);  // deleted meanwhile
        return;
    }
    it->imagePath = imagePath;
    version++;
    saveLocked();
}

DownloadManager::TransferResult DownloadManager::transferOnce(const std::string& id, const std::string& url,
                                                              const std::string& path, std::string& error,
                                                              bool& madeProgress) {
    std::error_code ec;
    std::filesystem::create_directories(getDownloadDirectory(), ec);

    DownloadTransfer t;
    t.manager = this;
    t.id      = id;
    t.path    = path;
    t.base    = fileSize(path);
    t.file    = std::fopen(path.c_str(), "ab");
    if (!t.file) {
        error = brls::getStr("tsvitch/download/error_file");
        return TransferResult::FATAL_ERROR;
    }
    t.curl = curl_easy_init();
    if (!t.curl) {
        std::fclose(t.file);
        error = "curl";
        return TransferResult::NETWORK_ERROR;
    }
    t.lastUpdate = t.lastSave = t.speedTime = std::chrono::steady_clock::now();
    t.speedBytes                            = t.base;

    CURL* curl = t.curl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TsVitch/1.0");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    // Nothing for a minute: the connection is dead
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    if (t.base > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(t.base));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, transferWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transferProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);

    CURLcode res = curl_easy_perform(curl);
    long code    = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    if (t.file) std::fclose(t.file);

    if (res == CURLE_RANGE_ERROR && t.base > 0) {
        // The server cannot continue a download: the video is downloaded again from the start
        brls::Logger::warning("DownloadManager: {} cannot be continued, starting again", id);
        std::FILE* reset = std::fopen(path.c_str(), "wb");
        if (reset) std::fclose(reset);
        return transferOnce(id, url, path, error, madeProgress);
    }

    madeProgress = t.written > 0;
    size_t done  = t.base + t.written;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it != downloads.end()) {
            it->downloadedSize = done;
            if (t.total) it->totalSize = t.total;
            it->progress = percent(done, it->totalSize);
            it->speed    = 0;
            version++;
        }
    }

    if (res == CURLE_OK) {
        if (done > 0 && (t.total == 0 || done >= t.total)) return TransferResult::DONE;
        error = brls::getStr("tsvitch/download/error_network");
        return TransferResult::NETWORK_ERROR;
    }
    if (res == CURLE_ABORTED_BY_CALLBACK) return TransferResult::STOPPED;
    if (t.notVideo) {
        error = brls::getStr("tsvitch/download/error_not_video");
        return TransferResult::FATAL_ERROR;
    }
    if (t.writeError) {
        error = brls::getStr("tsvitch/download/error_file");
        return TransferResult::FATAL_ERROR;
    }
    if (res == CURLE_HTTP_RETURNED_ERROR) {
        if (code == 416 && t.base > 0) return TransferResult::DONE;  // the file already has every byte
        error = fmt::format("HTTP {}", code);
        if (code == 401 || code == 404 || code == 410) return TransferResult::FATAL_ERROR;
        return TransferResult::NETWORK_ERROR;
    }
    error = brls::getStr("tsvitch/download/error_network");
    brls::Logger::warning("DownloadManager: {} curl error {}", id, curl_easy_strerror(res));
    return TransferResult::NETWORK_ERROR;
}

bool DownloadManager::onTransferProgress(DownloadTransfer& t) {
    if (shouldStop) return false;
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto it = findDownload(t.id);
    if (it == downloads.end() || it->status != DownloadStatus::DOWNLOADING) return false;
    if (now - t.lastUpdate < 500ms) return true;
    t.lastUpdate = now;

    size_t done    = t.base + t.written;
    double seconds = std::chrono::duration<double>(now - t.speedTime).count();
    if (seconds >= 2.0) {
        double current = done > t.speedBytes ? static_cast<double>(done - t.speedBytes) / seconds : 0.0;
        t.speed        = t.speed > 0 ? t.speed * 0.6 + current * 0.4 : current;
        t.speedTime    = now;
        t.speedBytes   = done;
    }
    it->downloadedSize = done;
    it->totalSize      = t.total;
    it->progress       = percent(done, t.total);
    it->speed          = t.speed;
    version++;
    if (now - t.lastSave > 15s) {
        t.lastSave = now;
        saveLocked();
    }
    return true;
}

void DownloadManager::finishDownload(const std::string& id, DownloadStatus status, const std::string& error) {
    std::string title;
    {
        std::lock_guard<std::mutex> lock(downloadsMutex);
        auto it = findDownload(id);
        if (it == downloads.end() || it->status != DownloadStatus::DOWNLOADING) return;
        it->status = status;
        it->error  = error;
        it->speed  = 0;
        if (status == DownloadStatus::COMPLETED) {
            it->downloadedSize = fileSize(it->localPath);
            it->totalSize      = it->downloadedSize;
            it->progress       = 100.0f;
        }
        title = it->title;
        version++;
        saveLocked();
    }
    brls::Logger::info("DownloadManager: {} {}", id, status == DownloadStatus::COMPLETED ? "completed" : "failed");
    brls::sync([status, title]() {
        brls::Application::notify(brls::getStr(status == DownloadStatus::COMPLETED ? "tsvitch/download/completed_title"
                                                                                  : "tsvitch/download/failed_title",
                                               title));
    });
}

void DownloadManager::loadDownloads() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (downloadsLoaded) return;
    downloadsLoaded = true;

    std::string path = getDownloadsStatePath();
    std::ifstream file(path);
    if (!file) file.open(path + ".tmp");  // a save stopped between its two steps
    if (!file) return;
    json list = json::parse(file, nullptr, false);
    if (!list.is_array()) {
        brls::Logger::error("DownloadManager: downloads.json is damaged");
        return;
    }
    for (const auto& entry : list) {
        try {
            DownloadItem item;
            item.id  = entry.value("id", std::string{});
            item.url = trim(entry.value("url", std::string{}));
            if (item.id.empty() || item.url.empty()) continue;
            item.title          = entry.value("title", std::string{"Video"});
            item.localPath      = entry.value("localPath", std::string{});
            item.imageUrl       = entry.value("imageUrl", std::string{});
            item.imagePath      = entry.value("imagePath", std::string{});
            item.status         = static_cast<DownloadStatus>(entry.value("status", 2));
            item.progress       = entry.value("progress", 0.0f);
            item.totalSize      = entry.value("totalSize", size_t{0});
            item.downloadedSize = entry.value("downloadedSize", size_t{0});
            item.error          = entry.value("error", std::string{});
            // Nothing starts by itself: an interrupted download waits for "Continue"
            if (isRunning(item.status)) item.status = DownloadStatus::PAUSED;
            if (item.status != DownloadStatus::COMPLETED) {
                item.downloadedSize = fileSize(item.localPath);
                item.progress       = percent(item.downloadedSize, item.totalSize);
            }
            downloads.push_back(std::move(item));
        } catch (const std::exception& e) {
            brls::Logger::error("DownloadManager: skipped a damaged download entry: {}", e.what());
        }
    }
    version++;
    brls::Logger::info("DownloadManager: {} downloads loaded", downloads.size());
}

void DownloadManager::saveLocked() const {
    json list = json::array();
    for (const auto& d : downloads) {
        list.push_back({{"id", d.id},
                        {"title", d.title},
                        {"url", d.url},
                        {"localPath", d.localPath},
                        {"imageUrl", d.imageUrl},
                        {"imagePath", d.imagePath},
                        {"status", static_cast<int>(d.status)},
                        {"progress", d.progress},
                        {"totalSize", d.totalSize},
                        {"downloadedSize", d.downloadedSize},
                        {"error", d.error}});
    }
    std::error_code ec;
    std::filesystem::create_directories(getDownloadDirectory(), ec);
    std::string path = getDownloadsStatePath();
    std::string temp = path + ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file) return;
        file << list.dump(1);
        if (!file.good()) return;
    }
    // The SD card cannot rename over an existing file
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temp, path, ec);
}

std::vector<DownloadItem>::iterator DownloadManager::findDownload(const std::string& id) {
    return std::find_if(downloads.begin(), downloads.end(), [&id](const DownloadItem& d) { return d.id == id; });
}

std::vector<DownloadItem>::const_iterator DownloadManager::findDownload(const std::string& id) const {
    return std::find_if(downloads.begin(), downloads.end(), [&id](const DownloadItem& d) { return d.id == id; });
}

std::string DownloadManager::generateDownloadId() const {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> digit(0, 15);
    std::string id;
    do {
        id.clear();
        for (int i = 0; i < 16; i++) id += "0123456789abcdef"[digit(generator)];
    } while (findDownload(id) != downloads.end());
    return id;
}

std::string DownloadManager::getDownloadsStatePath() const { return getDownloadDirectory() + "/downloads.json"; }
