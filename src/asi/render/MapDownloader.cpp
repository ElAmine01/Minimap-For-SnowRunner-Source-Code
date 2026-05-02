#define NOMINMAX
#include "MapDownloader.h"
#include "../core/Logger.h"

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <thread>
#include <fstream>
#include <vector>

namespace snowmap::render {

std::atomic<bool> MapDownloader::s_isDownloading{false};
std::atomic<bool> MapDownloader::s_isFinished{false};
std::atomic<bool> MapDownloader::s_isFailed{false};

void MapDownloader::StartDownload(const std::string& levelId, const std::string& savePath) {
    if (s_isDownloading.load()) return;
    
    s_isDownloading.store(true);
    s_isFinished.store(false);
    s_isFailed.store(false);

    std::thread([levelId, savePath]() {
        SM_INFO("MapDownloader: Debut du telechargement pour %s...", levelId.c_str());
        
        if (DownloadInternal(levelId, savePath)) {
            SM_INFO("MapDownloader: Telechargement reussi pour %s !", levelId.c_str());
            s_isFinished.store(true);
        } else {
            SM_ERROR("MapDownloader: Echec du telechargement pour %s.", levelId.c_str());
            s_isFailed.store(true);
        }
        
        s_isDownloading.store(false);
    }).detach();
}

bool MapDownloader::DownloadInternal(const std::string& levelId, const std::string& savePath) {
    HINTERNET hSession = WinHttpOpen(L"SnowMap/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, L"cdn2.maprunner.info", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    std::wstring wLevelId(levelId.begin(), levelId.end());
    std::wstring urlPath = L"/terrainimages/" + wLevelId + L"_map.png";

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    std::wstring headers = L"Referer: https://3d.maprunner.info/\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    bool result = false;

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD statusCode = 0;
            DWORD dwSize = sizeof(statusCode);
            
            // LA CORRECTION EST ICI : WINHTTP_HEADER_NAME_BY_INDEX
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

            if (statusCode == 200) {
                std::ofstream file(savePath, std::ios::binary);
                if (file.is_open()) {
                    DWORD bytesAvailable = 0;
                    do {
                        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) break;
                        if (bytesAvailable == 0) break;

                        std::vector<char> buffer(bytesAvailable);
                        DWORD bytesRead = 0;
                        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                            file.write(buffer.data(), bytesRead);
                        }
                    } while (bytesAvailable > 0);
                    file.close();
                    result = true;
                }
            } else {
                SM_ERROR("MapDownloader: Erreur HTTP %u", statusCode);
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

void MapDownloader::Reset() {
    s_isFinished.store(false);
    s_isFailed.store(false);
}

} // namespace snowmap::render