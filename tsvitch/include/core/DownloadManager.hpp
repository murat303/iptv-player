#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <borealis/core/singleton.hpp>
#include <borealis/core/event.hpp>

// The numbers are stored in downloads.json
enum class DownloadStatus { PENDING = 0, DOWNLOADING = 1, PAUSED = 2, COMPLETED = 3, FAILED = 4, CANCELLED = 5 };

struct DownloadItem {
    std::string id;
    std::string title;
    std::string url;
    std::string localPath;
    std::string imageUrl;
    std::string imagePath;  // cover saved by older versions (removed together with the video)
    DownloadStatus status = DownloadStatus::PENDING;
    float progress        = 0.0f;
    size_t totalSize      = 0;  // 0 while the size is unknown
    size_t downloadedSize = 0;
    double speed          = 0;  // bytes per second while downloading (not saved)
    std::string error;
};

struct DownloadTransfer;

/// Downloads movies and episodes to <config dir>/downloads. Downloads run one after the other (IPTV
/// accounts usually allow a single connection); the list is saved in downloads.json and a stopped download
/// continues where its file ends.
class DownloadManager : public brls::Singleton<DownloadManager> {
public:
    DownloadManager();
    ~DownloadManager();

    /// Adds a download and starts it when no other download runs; returns its id ("" when it cannot be added).
    /// A url that is already in the list is not added twice: its id is returned.
    std::string startDownload(const std::string& title, const std::string& url, const std::string& imageUrl);

    void pauseDownload(const std::string& id);

    /// Continues a paused or failed download
    void resumeDownload(const std::string& id);

    /// Stops the download and removes it with its file
    void deleteDownload(const std::string& id);

    std::vector<DownloadItem> getAllDownloads() const;

    /// Empty item (no id) when the download does not exist
    DownloadItem getDownload(const std::string& id) const;

    /// The download that runs or waits to run, if there is one
    bool getActiveDownload(DownloadItem& item) const;

    /// The download of this url, if the list has one
    bool findByUrl(const std::string& url, DownloadItem& item) const;

    /// Changes whenever the list or the state of a download changes (the downloads screen watches it)
    uint64_t getVersion() const { return version.load(); }

    /// Reads downloads.json (only the first call does something)
    void loadDownloads();

    std::string getDownloadDirectory() const;

    /// Called by curl while data arrives; false stops the transfer (download paused or deleted, app closing)
    bool onTransferProgress(DownloadTransfer& transfer);

private:
    enum class TransferResult { DONE, STOPPED, NETWORK_ERROR, FATAL_ERROR };

    // Starts the worker thread when a download waits and no worker runs
    void runQueue();
    // Worker thread: runs the waiting downloads one after the other
    void workerLoop(std::string id);
    // Downloads one item, retrying after network errors
    void runDownload(const std::string& id);
    // Saves the cover next to the video, so the downloads tab shows it without internet
    void fetchCover(const std::string& id);
    TransferResult transferOnce(const std::string& id, const std::string& url, const std::string& path,
                                std::string& error, bool& madeProgress);
    // True while the download exists, is not paused and the app is not closing
    bool stillDownloading(const std::string& id) const;
    // Sets the final state of a download and tells the user
    void finishDownload(const std::string& id, DownloadStatus status, const std::string& error);
    // Pauses the running download and waits a little for the worker (the app is closing)
    void shutdown();

    void saveLocked() const;  // downloadsMutex must be held
    std::vector<DownloadItem>::iterator findDownload(const std::string& id);
    std::vector<DownloadItem>::const_iterator findDownload(const std::string& id) const;
    std::string generateDownloadId() const;
    std::string getDownloadsStatePath() const;

    std::vector<DownloadItem> downloads;
    mutable std::mutex downloadsMutex;
    std::atomic<uint64_t> version{1};
    bool downloadsLoaded = false;

    std::thread worker;
    bool workerBusy = false;  // guarded by downloadsMutex
    std::atomic<bool> shouldStop{false};

    brls::Event<>::Subscription exitSubscription;
};
