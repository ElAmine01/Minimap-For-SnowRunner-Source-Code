#pragma once
#include <string>
#include <atomic>

namespace snowmap::render {

class MapDownloader {
public:
    // Lance le téléchargement en arrière-plan
    static void StartDownload(const std::string& levelId, const std::string& savePath);

    // État du téléchargement
    static bool IsDownloading() { return s_isDownloading.load(); }
    static bool IsFinished()    { return s_isFinished.load(); }
    static bool IsFailed()      { return s_isFailed.load(); }

    // Réinitialise l'état (quand on change de carte)
    static void Reset();

private:
    static bool DownloadInternal(const std::string& levelId, const std::string& savePath);

    static std::atomic<bool> s_isDownloading;
    static std::atomic<bool> s_isFinished;
    static std::atomic<bool> s_isFailed;
};

} // namespace snowmap::render