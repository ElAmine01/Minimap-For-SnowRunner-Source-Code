#pragma once
#include <string>
#include <atomic>

namespace snowmap::render {

/// Background downloader for CDN map images.
class MapDownloader {
public:
    /// Start background download for the given level map.
    static void StartDownload(const std::string& levelId, const std::string& savePath);

    /// True while a download thread is active.
    static bool IsDownloading() { return s_isDownloading.load(); }
    /// True once the last download completed successfully.
    static bool IsFinished()    { return s_isFinished.load(); }
    /// True once the last download failed.
    static bool IsFailed()      { return s_isFailed.load(); }

    /// Reset state when switching levels.
    static void Reset();

private:
    static bool DownloadInternal(const std::string& levelId, const std::string& savePath);

    static std::atomic<bool> s_isDownloading;
    static std::atomic<bool> s_isFinished;
    static std::atomic<bool> s_isFailed;
};

} // namespace snowmap::render