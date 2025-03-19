#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include "FileMonitor.h"
#include "FileDownloader.h"
#include "URLGenerator.h"
#include "ZipManager.h"
#include "UpgradePathManager.h"
#include "WindowsServiceManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

class UpdateManager {
public:
    UpdateManager(const std::string& region, const std::string& customerId, const std::string& siteId,
        const std::string& blobName, const std::string& jsonHashFile,
        const std::string& downloadPath, const std::string& extractPath)
        : urlGenerator(region, customerId, siteId, blobName),
        configMonitor(downloadPath, jsonHashFile),
        downloadPath(downloadPath), extractPath(extractPath) {
    }

    /**
     * @brief Initiates the initial installation process by downloading and extracting a ZIP file.
     *
     * This function attempts to download the installation package from a valid URL,
     * using optional proxy settings if available. If the installation is required,
     * the downloaded package is extracted. Otherwise, the downloaded file is deleted.
     *
     * @return true if the installation was successful, false if it was not needed or if an error occurred.
     */
    bool PerformInitialInstallation() {
        try {
            std::string url = urlGenerator.getValidUrl();
            if (url.empty()) {
                LOG_ERROR("No valid URL found for initial installation.");

                return false;
            }

            UpgradePathManager pathManager;
            std::string proxyConfig = pathManager.GetProxyFilePath();
            FileDownloader downloader(url, downloadPath);
            if (!downloader.downloadWithOptionalProxy(url,downloadPath,proxyConfig)) {
                LOG_ERROR("Failed to download the installation file: {}", downloadPath);

                return false;
            }

            bool shouldExtract = configMonitor.InitialInstall();
            if (shouldExtract) {
                LOG_INFO("Initial installation required, extracting...");

                if (!ExtractUpdate()) {
                    LOG_ERROR("Failed to extract installation package from {}", downloadPath);

                    return false;
                }
                return true;
            }

            LOG_INFO("Deleting unnecessary ZIP file.");

            fs::remove(downloadPath);
            return false;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in PerformInitialInstallation: {}", e.what());

            return false;
        }
    }



    /**
     * @brief Main update logic: Downloads the ZIP file, checks if it's changed, and extracts if needed.
     * @return True if update was applied (file extracted), false otherwise.
     */
    bool PerformUpdate() {
        try {
            std::string url = urlGenerator.getValidUrl();
            std::cout << url << std::endl;
            if (url.empty()) {
                LOG_ERROR("No valid URL found for update");

                return false;
            }

            FileDownloader downloader(url, downloadPath);
            /*if (!downloader.download()) {
                spdlog::error("Failed to download the update file: {}", downloadPath);
                return false;
            }*/
            UpgradePathManager pathManager;
            std::string proxyConfig = pathManager.GetProxyFilePath();
            if (!downloader.downloadWithOptionalProxy(url, downloadPath, proxyConfig)) {
                LOG_ERROR("Failed to download the installation file: {}", downloadPath);

                return false;
            }

            bool shouldExtract = configMonitor.ShouldRestartService();
            if (!shouldExtract) {
                UpgradePathManager path;

                std::string exe1 = ConvertWStringToString(path.GetService1TargetPath());
                std::string exe2 = ConvertWStringToString(path.GetService2TargetPath());
                std::string jsonCheck = path.GetServiceHashFilePath();

                bool exe1Changed = !IsFileUnchanged(exe1, jsonCheck);
                bool exe2Changed = !IsFileUnchanged(exe2, jsonCheck);

                if (exe1Changed || exe2Changed) {
                    LOG_INFO("One or more files have changed. Extraction is required.");
                    shouldExtract = true;
                }
                else {
                    LOG_INFO("All files are unchanged. No extraction needed.");
                }
            }
            if (shouldExtract) {
                LOG_INFO("New update detected, extracting...");

                if (!ExtractUpdate()) {
                    LOG_ERROR("Failed to extract update from {}", downloadPath);

                    return false;
                }
                return true;
            }

            LOG_INFO("Update file is unchanged. Deleting unnecessary ZIP file.");

            fs::remove(downloadPath);
            return false;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in PerformUpdate: {}", e.what());

            return false;
        }
    }

    /**
     * @brief Determines if a full reinstall is required.
     *
     * This function checks whether the update process requires a complete reinstallation
     * by analyzing the update type.
     *
     * @return true if a full reinstall is required, false otherwise.
     */
    bool NeedsFullReinstall() {
        return DetermineUpdateType() == UpdateType::FULL_REINSTALL;
    }

    /**
     * @brief Cleans the extracted folder by removing all unnecessary files.
     *
     * This function deletes all extracted files and directories except `service_hashes.json`.
     * It ensures that the folder is cleaned up after an update process to avoid conflicts.
     *
     * If the folder does not exist, it logs a warning and skips cleanup.
     */

    void CleanExtractedFolder() {
        if (!fs::exists(extractPath)) {
            LOG_WARN("Extract folder '{}' does not exist. Skipping cleanup.", extractPath);

            return;
        }

        try {
            LOG_INFO("Cleaning extracted folder: {}", extractPath);


            for (const auto& entry : fs::directory_iterator(extractPath)) {
                if (entry.path().filename() == "service_hashes.json") {
                    LOG_INFO("Skipping file: {}", entry.path().string());

                    continue;
                }

                fs::remove_all(entry);
                LOG_INFO("Deleted: {}", entry.path().string());

            }

            LOG_INFO("Extracted folder '{}' cleaned successfully.", extractPath);

        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to clean extracted folder '{}': {}", extractPath, e.what());

        }
    }


private:
    enum class UpdateType {
        FULL_REINSTALL,
        RESTART_ONLY
    };

    URLGenerator urlGenerator;
    ConfigFileMonitor configMonitor;
    std::string downloadPath;
    std::string extractPath;
    ZipManager zipManager;

    /**
     * @brief Checks if a file has remained unchanged based on its SHA-256 hash.
     *
     * This function computes the SHA-256 hash of the specified file and compares it
     * with the previously stored hash in a JSON file. If the file is missing or the
     * hash does not match, it is considered changed.
     *
     * @param filePath The path to the file being checked.
     * @param jsonFilePath The path to the JSON file storing file hashes.
     * @return true if the file is unchanged, false otherwise.
     */
    bool IsFileUnchanged(const std::string& filePath, const std::string& jsonFilePath) {
        try {
            FileHasher hasher(jsonFilePath);

            if (!fs::exists(filePath)) {
                LOG_ERROR("File does not exist: {}", filePath);
                return false;
            }

            auto currentHash = hasher.GetFileSHA256(filePath);
            if (!currentHash) {
                LOG_ERROR("Failed to compute hash for file: {}", filePath);
                return false;
            }

            if (!fs::exists(jsonFilePath)) {
                LOG_WARN("JSON file '{}' does not exist.", jsonFilePath);
                return false;
            }

            auto storedHash = hasher.GetStoredFileHash(filePath);
            if (!storedHash) {
                LOG_WARN("Stored hash not found or invalid for file: {}", filePath);
                return false;
            }

            LOG_INFO("Computed hash: {}", *currentHash);

            LOG_INFO("Stored hash: {}", *storedHash);

            if (*currentHash == *storedHash) {
                LOG_INFO("File '{}' is unchanged.", filePath);
                return true;
            }

            LOG_INFO("File '{}' has changed.", filePath);
            return false;

        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in IsFileUnchanged for '{}': {}", filePath, e.what());
            return false;
        }
        catch (...) {
            LOG_ERROR("Unknown error in IsFileUnchanged for '{}'.", filePath);
            return false;
        }
    }

    /**
     * @brief Extracts the downloaded ZIP file to the target directory.
     * @return True if extraction is successful, false otherwise.
     */
    bool ExtractUpdate() {
        if (!fs::exists(downloadPath)) {
            LOG_ERROR("ZIP file not found: {}", downloadPath);

            return false;
        }

        if (!zipManager.ExtractArchiveToFolder(downloadPath, extractPath)) {
            LOG_ERROR("Failed to extract ZIP file: {}", downloadPath);

            return false;
        }

        LOG_INFO("Successfully extracted update to {}", extractPath);


        try {
            fs::remove(downloadPath);
            LOG_INFO("Deleted ZIP file after successful extraction: {}", downloadPath);

        }
        catch (const std::exception& e) {
            LOG_WARN("Failed to delete ZIP file '{}': {}", downloadPath, e.what());

        }
        configMonitor.AcknowledgeRestart();
        //std::this_thread::sleep_for(std::chrono::minutes(5));

        return true;
    }

    /**
     * @brief Determines whether a full reinstall or just a restart is needed.
     * @return UpdateType enum (FULL_REINSTALL or RESTART_ONLY).
     */
    UpdateType DetermineUpdateType() {
        std::string updateConfigPath = extractPath + "\\ncrv_dcs_streaming_service_upgrade_manager\\upgrade_config.json";

        LOG_INFO("Checking update configuration at: {}", updateConfigPath);


        if (!fs::exists(updateConfigPath)) {
            LOG_WARN("No 'update_config.json' found, assuming service restart only.");

            return UpdateType::RESTART_ONLY; 
        }

        try {
            std::ifstream file(updateConfigPath);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open 'update_config.json' for reading.");

                return UpdateType::RESTART_ONLY;
            }

            json config;
            file >> config;
            file.close();

            if (config.contains("full_reinstall") && config["full_reinstall"].is_boolean()) {
                bool fullReinstall = config["full_reinstall"];
                LOG_INFO("full_reinstall: {}", fullReinstall);
            }

            if (config.contains("reason") && config["reason"].is_string()) {
                std::string reason = config["reason"];
                LOG_INFO("reason: {}", reason);
            }

            if (config.contains("required_version") && config["required_version"].is_string()) {
                std::string requiredVersion = config["required_version"];
                LOG_INFO("required_version: {}", requiredVersion);
            }

            if (config.contains("timestamp") && config["timestamp"].is_string()) {
                std::string timestamp = config["timestamp"];
                LOG_INFO("timestamp: {}", timestamp);
            }

            if (config.contains("full_reinstall") && config["full_reinstall"].is_boolean()) {
                bool fullReinstall = config["full_reinstall"];
                return fullReinstall ? UpdateType::FULL_REINSTALL : UpdateType::RESTART_ONLY;
            }

            LOG_WARN("Invalid 'update_config.json' format. Assuming restart only.");

            return UpdateType::RESTART_ONLY;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error reading 'update_config.json': {}", e.what());

            return UpdateType::RESTART_ONLY;
        }
    }
};

#endif // UPDATEMANAGER_H
