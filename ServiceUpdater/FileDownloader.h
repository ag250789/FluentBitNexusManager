#ifndef FILEDOWNLOADER_H
#define FILEDOWNLOADER_H

#include <fstream>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <mutex>
#include <memory>
#include <curl/curl.h>
#include "Proxy.h"

namespace fs = std::filesystem;

class FileDownloader {
public:
    FileDownloader(const std::string& url, const std::string& destinationPath, int maxRetries = 3, int timeoutSeconds = 60)
        : url(url), destinationPath(destinationPath), maxRetries(maxRetries), timeoutSeconds(timeoutSeconds) {
    }

    /**
     * @brief Callback funkcija za upis podataka u fajl tokom preuzimanja.
     */
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        if (!contents || !userp) {
            //spdlog::error("WriteCallback: Invalid pointer passed.");
            LOG_ERROR("WriteCallback: Invalid pointer passed.");
            return 0;
        }

        std::ofstream* file = static_cast<std::ofstream*>(userp);
        if (!file->is_open()) {
            //spdlog::error("WriteCallback: File is not open for writing.");
            LOG_ERROR("WriteCallback: File is not open for writing.");
            return 0;
        }
        file->write(static_cast<const char*>(contents), size * nmemb);
        return size * nmemb;
    }

    /**
     * @brief Proverava da li postoji dovoljno prostora na disku pre preuzimanja.
     */
    bool hasSufficientDiskSpace(uint64_t fileSize) {
        try {
            auto spaceInfo = fs::space(fs::path(destinationPath).parent_path());
            return spaceInfo.available >= fileSize;
        }
        catch (const std::exception& e) {
            //spdlog::error("Failed to check disk space: {}", e.what());
            LOG_ERROR("Failed to check disk space: {}", e.what());
            return false;
        }
    }

    /**
     * @brief Sigurno preuzima fajl koriste?i `libcurl`, sa retry mehanizmom.
     */
    bool download() {
        std::lock_guard<std::mutex> lock(downloadMutex);

        try {
            fs::path dir = fs::path(destinationPath).parent_path();
            if (!fs::exists(dir)) {
                //spdlog::info("Creating directory: {}", dir.string());
                LOG_INFO("Creating directory: {}", dir.string());

                if (!fs::create_directories(dir)) {
                    //spdlog::error("Failed to create directory: {}", dir.string());
                    LOG_ERROR("Failed to create directory: {}", dir.string());

                    return false;
                }
            }

            int attempt = 0;
            while (attempt < maxRetries) {
                if (attempt > 0) {
                    //spdlog::warn("Retrying download... Attempt: {}", attempt + 1);
                    LOG_WARN("Retrying download... Attempt: {}", attempt + 1);

                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }

                std::ofstream outputFile(destinationPath, std::ios::binary | std::ios::trunc);
                if (!outputFile.is_open()) {
                    //spdlog::error("Failed to open file: {}", destinationPath);
                    LOG_ERROR("Failed to open file: {}", destinationPath);

                    return false;
                }

                struct CurlDeleter {
                    void operator()(CURL* curl) const {
                        if (curl) {
                            curl_easy_cleanup(curl);
                        }
                    }
                };

                std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
                if (!curl) {
                    //spdlog::error("Failed to initialize CURL");
                    LOG_ERROR("Failed to initialize CURL");

                    return false;
                }

                curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &outputFile);
                curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeoutSeconds);
                curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);

                CURLcode res = curl_easy_perform(curl.get());
                long response_code = 0;
                curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
                outputFile.close();

                if (res == CURLE_OK && response_code == 200) {
                    //spdlog::info("Download successful: {}", destinationPath);
                    LOG_INFO("Download successful: {}", destinationPath);

                    return true;
                }
                else {
                    HandleCurlError(res, response_code);
                    if (response_code >= 500 && response_code < 600) {
                        attempt++;
                        continue;  // Retry for server errors (5xx)
                    }
                    return false;  // Other errors, no retry
                }
            }

            //spdlog::error("Download failed after {} attempts", maxRetries);
            LOG_ERROR("Download failed after {} attempts", maxRetries);

            return false;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception during download: {}", e.what());
            LOG_ERROR("Exception during download: {}", e.what());

            return false;
        }
    }

    /**
     * @brief Preuzima fajl koriste?i proxy ako je konfigurisan.
     */
    bool downloadWithOptionalProxy(const std::string& url, const std::string& destinationPath, const std::string& proxyConfigPath = "") {
        try {
            //spdlog::info("Starting download for URL: {}", url);
            LOG_INFO("Starting download.");


            if (!proxyConfigPath.empty() && fs::exists(proxyConfigPath)) {
                //spdlog::info("Proxy configuration found. Using proxy for download.");
                LOG_INFO("Proxy configuration found. Using proxy for download.");

                Proxy proxy(proxyConfigPath);

                if (!proxy.isProxyEnabled()) {
                    //spdlog::warn("Proxy is disabled in the configuration. Falling back to direct download.");
                    LOG_WARN("Proxy is disabled in the configuration. Falling back to direct download.");

                }
                else {
                    return proxy.proxyDownload(url, destinationPath);
                }
            }
            else {
                //spdlog::info("No proxy configuration found. Using direct download.");
                LOG_INFO("No proxy configuration found. Using direct download.");

            }

            // Koristi obi?an FileDownloader ako proxy nije dostupan
            FileDownloader downloader(url, destinationPath);
            return downloader.download();
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception occurred in downloadWithOptionalProxy: {}", e.what());
            LOG_ERROR("Exception occurred in downloadWithOptionalProxy: {}", e.what());

            return false;
        }
        catch (...) {
            //spdlog::error("Unknown exception occurred in downloadWithOptionalProxy.");
            LOG_ERROR("Unknown exception occurred in downloadWithOptionalProxy.");

            return false;
        }
    }

private:
    std::string url;             ///< URL fajla koji se preuzima.
    std::string destinationPath; ///< Putanja gde ?e se fajl sa?uvati.
    int maxRetries;              ///< Maksimalan broj pokušaja.
    int timeoutSeconds;          ///< Timeout za preuzimanje.
    std::mutex downloadMutex;    ///< Mutex za sinhronizaciju.

    /**
     * @brief Obrada razli?itih HTTP status kodova i CURL grešaka.
     */
    void HandleCurlError(CURLcode res, long response_code) {
        if (res != CURLE_OK) {
            //spdlog::error("CURL error: {} - {}", static_cast<int>(res), std::string(curl_easy_strerror(res)));
            LOG_ERROR("CURL error: {} - {}", static_cast<int>(res), std::string(curl_easy_strerror(res)));

        }

        switch (response_code) {
        case 404:
            //spdlog::error("File not found (404): {}", url);
            LOG_ERROR("File not found (404).");

            break;
        case 403:
            //spdlog::error("Access forbidden (403): {}", url);
            LOG_ERROR("Access forbidden (403).");

            break;
        default:
            if (response_code >= 500 && response_code <= 599) {
                //spdlog::warn("Server error ({}), retrying...", response_code);
                LOG_WARN("Server error ({}), retrying...", response_code);

            }
            else {
                //spdlog::error("Unexpected HTTP response ({}): {}", response_code, url);
                LOG_ERROR("Unexpected HTTP response ({}).", response_code);

            }
        }
    }
};

#endif  // FILEDOWNLOADER_H
