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
     * @brief Pokre?e proces inicijalne instalacije: preuzima ZIP, raspakuje ga i potvr?uje instalaciju.
     * @return True ako je instalacija uspešna, false ako nije potrebna ili ako do?e do greške.
     */
    bool PerformInitialInstallation() {
        try {
            std::string url = urlGenerator.getValidUrl();
            if (url.empty()) {
                //spdlog::error("No valid URL found for initial installation.");
                LOG_ERROR("No valid URL found for initial installation.");

                return false;
            }

            UpgradePathManager pathManager;
            std::string proxyConfig = pathManager.GetProxyFilePath();
            FileDownloader downloader(url, downloadPath);
            if (!downloader.downloadWithOptionalProxy(url,downloadPath,proxyConfig)) {
                //spdlog::error("Failed to download the installation file: {}", downloadPath);
                LOG_ERROR("Failed to download the installation file: {}", downloadPath);

                return false;
            }

            bool shouldExtract = configMonitor.InitialInstall();
            if (shouldExtract) {
                //spdlog::info("Initial installation required, extracting...");
                LOG_INFO("Initial installation required, extracting...");

                if (!ExtractUpdate()) {
                    //spdlog::error("Failed to extract installation package from {}", downloadPath);
                    LOG_ERROR("Failed to extract installation package from {}", downloadPath);

                    return false;
                }
                return true;
            }

            //spdlog::info("Deleting unnecessary ZIP file.");
            LOG_INFO("Deleting unnecessary ZIP file.");

            fs::remove(downloadPath);
            return false;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in PerformInitialInstallation: {}", e.what());
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
                //spdlog::error("No valid URL found for update");
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
                //spdlog::error("Failed to download the installation file: {}", downloadPath);
                LOG_ERROR("Failed to download the installation file: {}", downloadPath);

                return false;
            }

            bool shouldExtract = configMonitor.ShouldRestartService();
            if (!shouldExtract) {
                UpgradePathManager path;

                std::string exe1 = ConvertWStringToString(path.GetService1TargetPath());
                std::string exe2 = ConvertWStringToString(path.GetService2TargetPath());
                std::string jsonCheck = path.GetServiceHashFilePath();

                // Ako je BILO KOJI od fajlova promenjen, postavi shouldExtract na true
                bool exe1Changed = !IsFileUnchanged(exe1, jsonCheck);
                bool exe2Changed = !IsFileUnchanged(exe2, jsonCheck);

                if (exe1Changed || exe2Changed) {
                    //spdlog::info("One or more files have changed. Extraction is required.");
                    LOG_INFO("One or more files have changed. Extraction is required.");
                    shouldExtract = true;
                }
                else {
                    //spdlog::info("All files are unchanged. No extraction needed.");
                    LOG_INFO("All files are unchanged. No extraction needed.");
                }
            }
            if (shouldExtract) {
                //spdlog::info("New update detected, extracting...");
                LOG_INFO("New update detected, extracting...");

                if (!ExtractUpdate()) {
                    //spdlog::error("Failed to extract update from {}", downloadPath);
                    LOG_ERROR("Failed to extract update from {}", downloadPath);

                    return false;
                }
                return true;
            }

            //spdlog::info("Update file is unchanged. Deleting unnecessary ZIP file.");
            LOG_INFO("Update file is unchanged. Deleting unnecessary ZIP file.");

            fs::remove(downloadPath);
            return false;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in PerformUpdate: {}", e.what());
            LOG_ERROR("Exception in PerformUpdate: {}", e.what());

            return false;
        }
    }

    bool NeedsFullReinstall() {
        return DetermineUpdateType() == UpdateType::FULL_REINSTALL;
    }

    void CleanExtractedFolder() {
        if (!fs::exists(extractPath)) {
            //spdlog::warn("Extract folder '{}' does not exist. Skipping cleanup.", extractPath);
            LOG_WARN("Extract folder '{}' does not exist. Skipping cleanup.", extractPath);

            return;
        }

        try {
            //spdlog::info("Cleaning extracted folder: {}", extractPath);
            LOG_INFO("Cleaning extracted folder: {}", extractPath);


            for (const auto& entry : fs::directory_iterator(extractPath)) {
                // Proveravamo da li je fajl "service_hashes.json" i preska?emo ga
                if (entry.path().filename() == "service_hashes.json") {
                    //spdlog::info("Skipping file: {}", entry.path().string());
                    LOG_INFO("Skipping file: {}", entry.path().string());

                    continue;
                }

                fs::remove_all(entry);
                //spdlog::info("Deleted: {}", entry.path().string());
                LOG_INFO("Deleted: {}", entry.path().string());

            }

            //spdlog::info("Extracted folder '{}' cleaned successfully.", extractPath);
            LOG_INFO("Extracted folder '{}' cleaned successfully.", extractPath);

        }
        catch (const std::exception& e) {
            //spdlog::error("Failed to clean extracted folder '{}': {}", extractPath, e.what());
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

    bool IsFileUnchanged(const std::string& filePath, const std::string& jsonFilePath) {
        try {
            FileHasher hasher(jsonFilePath);

            // Provera da li fajl postoji
            if (!fs::exists(filePath)) {
                //spdlog::error("File does not exist: {}", filePath);
                LOG_ERROR("File does not exist: {}", filePath);
                return false;
            }

            // Izra?unavanje trenutnog hash-a fajla
            auto currentHash = hasher.GetFileSHA256(filePath);
            if (!currentHash) {
                //spdlog::error("Failed to compute hash for file: {}", filePath);
                LOG_ERROR("Failed to compute hash for file: {}", filePath);
                return false;
            }

            // Provera da li JSON fajl postoji
            if (!fs::exists(jsonFilePath)) {
                //spdlog::warn("JSON file '{}' does not exist.", jsonFilePath);
                LOG_WARN("JSON file '{}' does not exist.", jsonFilePath);
                return false;
            }

            // Dohvatanje sa?uvanog hash-a iz JSON-a
            auto storedHash = hasher.GetStoredFileHash(filePath);
            if (!storedHash) {
                //spdlog::warn("Stored hash not found or invalid for file: {}", filePath);
                LOG_WARN("Stored hash not found or invalid for file: {}", filePath);
                return false;
            }

            //spdlog::info("Computed hash: {}", *currentHash);
            LOG_INFO("Computed hash: {}", *currentHash);

            //spdlog::info("Stored hash: {}", *storedHash);
            LOG_INFO("Stored hash: {}", *storedHash);

            // Pore?enje trenutnog i sa?uvanog hash-a
            if (*currentHash == *storedHash) {
                //spdlog::info("File '{}' is unchanged.", filePath);
                LOG_INFO("File '{}' is unchanged.", filePath);
                return true;
            }

            //spdlog::info("File '{}' has changed.", filePath);
            LOG_INFO("File '{}' has changed.", filePath);
            return false;

        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in IsFileUnchanged for '{}': {}", filePath, e.what());
            LOG_ERROR("Exception in IsFileUnchanged for '{}': {}", filePath, e.what());
            return false;
        }
        catch (...) {
            //spdlog::error("Unknown error in IsFileUnchanged for '{}'.", filePath);
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
            //spdlog::error("ZIP file not found: {}", downloadPath);
            LOG_ERROR("ZIP file not found: {}", downloadPath);

            return false;
        }

        if (!zipManager.ExtractArchiveToFolder(downloadPath, extractPath)) {
            //spdlog::error("Failed to extract ZIP file: {}", downloadPath);
            LOG_ERROR("Failed to extract ZIP file: {}", downloadPath);

            return false;
        }

        //spdlog::info("Successfully extracted update to {}", extractPath);
        LOG_INFO("Successfully extracted update to {}", extractPath);


        // Brisemo ZIP fajl nakon raspakivanja
        try {
            fs::remove(downloadPath);
            //spdlog::info("Deleted ZIP file after successful extraction: {}", downloadPath);
            LOG_INFO("Deleted ZIP file after successful extraction: {}", downloadPath);

        }
        catch (const std::exception& e) {
            //spdlog::warn("Failed to delete ZIP file '{}': {}", downloadPath, e.what());
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

        //spdlog::info("Checking update configuration at: {}", updateConfigPath); 
        LOG_INFO("Checking update configuration at: {}", updateConfigPath);


        if (!fs::exists(updateConfigPath)) {
            //spdlog::warn("No 'update_config.json' found, assuming service restart only.");
            LOG_WARN("No 'update_config.json' found, assuming service restart only.");

            return UpdateType::RESTART_ONLY; // Ako fajl ne postoji, podrazumeva se restart
        }

        try {
            std::ifstream file(updateConfigPath);
            if (!file.is_open()) {
                //spdlog::error("Failed to open 'update_config.json' for reading.");
                LOG_ERROR("Failed to open 'update_config.json' for reading.");

                return UpdateType::RESTART_ONLY;
            }

            json config;
            file >> config;
            file.close();

            if (config.contains("full_reinstall") && config["full_reinstall"].is_boolean()) {
                bool fullReinstall = config["full_reinstall"];
                //spdlog::info("full_reinstall: {}", fullReinstall);
                LOG_INFO("full_reinstall: {}", fullReinstall);
            }

            if (config.contains("reason") && config["reason"].is_string()) {
                std::string reason = config["reason"];
                //spdlog::info("reason: {}", reason);
                LOG_INFO("reason: {}", reason);
            }

            if (config.contains("required_version") && config["required_version"].is_string()) {
                std::string requiredVersion = config["required_version"];
                //spdlog::info("required_version: {}", requiredVersion);
                LOG_INFO("required_version: {}", requiredVersion);
            }

            if (config.contains("timestamp") && config["timestamp"].is_string()) {
                std::string timestamp = config["timestamp"];
                //spdlog::info("timestamp: {}", timestamp);
                LOG_INFO("timestamp: {}", timestamp);
            }

            if (config.contains("full_reinstall") && config["full_reinstall"].is_boolean()) {
                bool fullReinstall = config["full_reinstall"];
                return fullReinstall ? UpdateType::FULL_REINSTALL : UpdateType::RESTART_ONLY;
            }

            //spdlog::warn("Invalid 'update_config.json' format. Assuming restart only.");
            LOG_WARN("Invalid 'update_config.json' format. Assuming restart only.");

            return UpdateType::RESTART_ONLY;
        }
        catch (const std::exception& e) {
            //spdlog::error("Error reading 'update_config.json': {}", e.what());
            LOG_ERROR("Error reading 'update_config.json': {}", e.what());

            return UpdateType::RESTART_ONLY;
        }
    }
};

#endif // UPDATEMANAGER_H
