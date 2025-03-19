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
     * @brief Callback function for writing data to a file during download.
     *
     * This function is used by `libcurl` to write received data into a file. It checks for valid
     * pointers before proceeding with writing data. If an error occurs, it logs the issue and
     * returns 0 to indicate failure.
     *
     * @param contents Pointer to the downloaded data.
     * @param size Size of a single data unit.
     * @param nmemb Number of data units.
     * @param userp Pointer to the file stream where data should be written.
     * @return The number of bytes written to the file, or 0 on failure.
     */
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        if (!contents || !userp) {
            LOG_ERROR("WriteCallback: Invalid pointer passed.");
            return 0;
        }

        std::ofstream* file = static_cast<std::ofstream*>(userp);
        if (!file->is_open()) {
            LOG_ERROR("WriteCallback: File is not open for writing.");
            return 0;
        }
        file->write(static_cast<const char*>(contents), size * nmemb);
        return size * nmemb;
    }

    /**
     * @brief Checks if there is enough free disk space before downloading a file.
     *
     * This function determines whether the available disk space is sufficient for the file
     * to be downloaded. It logs an error if the disk space check fails.
     *
     * @param fileSize The size of the file to be downloaded.
     * @return true if there is enough free space, false otherwise.
     */
    bool hasSufficientDiskSpace(uint64_t fileSize) {
        try {
            auto spaceInfo = fs::space(fs::path(destinationPath).parent_path());
            return spaceInfo.available >= fileSize;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to check disk space: {}", e.what());
            return false;
        }
    }

    /**
     * @brief Securely downloads a file using `libcurl` with a retry mechanism.
     *
     * This function downloads a file from a specified URL using `libcurl`. It ensures the
     * destination directory exists, handles errors gracefully, and retries downloading if a
     * recoverable server error occurs (5xx HTTP responses). The function also verifies the
     * response code and logs necessary information throughout the process.
     *
     * @return true if the download is successful, false otherwise.
     */
    bool download() {
        std::lock_guard<std::mutex> lock(downloadMutex);

        try {
            fs::path dir = fs::path(destinationPath).parent_path();
            if (!fs::exists(dir)) {
                LOG_INFO("Creating directory: {}", dir.string());

                if (!fs::create_directories(dir)) {
                    LOG_ERROR("Failed to create directory: {}", dir.string());

                    return false;
                }
            }

            int attempt = 0;
            while (attempt < maxRetries) {
                if (attempt > 0) {
                    LOG_WARN("Retrying download... Attempt: {}", attempt + 1);

                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }

                std::ofstream outputFile(destinationPath, std::ios::binary | std::ios::trunc);
                if (!outputFile.is_open()) {
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

            LOG_ERROR("Download failed after {} attempts", maxRetries);

            return false;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception during download: {}", e.what());

            return false;
        }
    }

    /**
     * @brief Downloads a file with optional proxy support.
     *
     * This function attempts to download a file from a given URL to a specified destination path.
     * If a proxy configuration file is provided and exists, it loads the proxy settings and determines
     * whether to use the proxy for downloading. If the proxy is enabled, the function delegates the
     * download to the proxy handler. Otherwise, it falls back to direct downloading.
     *
     * @param url The URL of the file to be downloaded.
     * @param destinationPath The local path where the downloaded file will be saved.
     * @param proxyConfigPath (Optional) Path to the proxy configuration file.
     * @return true if the download is successful, false otherwise.
     */
    bool downloadWithOptionalProxy(const std::string& url, const std::string& destinationPath, const std::string& proxyConfigPath = "") {
        try {
            LOG_INFO("Starting download.");


            if (!proxyConfigPath.empty() && fs::exists(proxyConfigPath)) {
                LOG_INFO("Proxy configuration found. Using proxy for download.");

                Proxy proxy(proxyConfigPath);

                if (!proxy.isProxyEnabled()) {
                    LOG_WARN("Proxy is disabled in the configuration. Falling back to direct download.");

                }
                else {
                    return proxy.proxyDownload(url, destinationPath);
                }
            }
            else {
                LOG_INFO("No proxy configuration found. Using direct download.");

            }

            FileDownloader downloader(url, destinationPath);
            return downloader.download();
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception occurred in downloadWithOptionalProxy: {}", e.what());

            return false;
        }
        catch (...) {
            LOG_ERROR("Unknown exception occurred in downloadWithOptionalProxy.");

            return false;
        }
    }

private:
    /**
     * @brief URL of the file to be downloaded.
     *
     * This string stores the web address from which the file will be downloaded.
     */
    std::string url;

    /**
     * @brief Destination path where the downloaded file will be saved.
     *
     * This string specifies the local file path where the downloaded file will be stored.
     */
    std::string destinationPath;

    /**
     * @brief Maximum number of retry attempts for the download.
     *
     * If the download fails due to a recoverable error (e.g., server-side errors),
     * the function will retry up to this specified number of times.
     */
    int maxRetries;

    /**
     * @brief Timeout duration (in seconds) for the download operation.
     *
     * This value defines the maximum time allowed for the download to complete
     * before it is considered a failure.
     */
    int timeoutSeconds;

    /**
     * @brief Mutex for synchronizing the download process.
     *
     * Ensures that multiple threads do not interfere with the download operation,
     * preventing race conditions and ensuring thread safety.
     */
    std::mutex downloadMutex;


    /**
     * @brief Handles `libcurl` errors and HTTP response codes.
     *
     * This function logs errors related to `libcurl` operations and processes HTTP response codes.
     * It provides specific error messages for common HTTP errors such as 404 (Not Found) and
     * 403 (Forbidden). Additionally, it issues a warning for server-side errors (5xx) to indicate
     * a possible retry scenario.
     *
     * @param res CURLcode representing the result of the `libcurl` operation.
     * @param response_code HTTP response code received from the server.
     */
    void HandleCurlError(CURLcode res, long response_code) {
        if (res != CURLE_OK) {
            LOG_ERROR("CURL error: {} - {}", static_cast<int>(res), std::string(curl_easy_strerror(res)));

        }

        switch (response_code) {
        case 404:
            LOG_ERROR("File not found (404).");

            break;
        case 403:
            LOG_ERROR("Access forbidden (403).");

            break;
        default:
            if (response_code >= 500 && response_code <= 599) {
                LOG_WARN("Server error ({}), retrying...", response_code);

            }
            else {
                LOG_ERROR("Unexpected HTTP response ({}).", response_code);

            }
        }
    }
};

#endif  // FILEDOWNLOADER_H
