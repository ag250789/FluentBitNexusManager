#ifndef MAIN_SERVICE_H
#define MAIN_SERVICE_H

#include "CommandLineParser.h"
#include "UpgradePathManager.h"
#include "InitialInstallationManager.h"
#include "ServiceUpgradeManager.h"
#include <libcron/Cron.h>
#include "spdlog/spdlog.h"
#include <iostream>
#include <stdexcept>

using namespace libcron;
using namespace std;

class MainService {
public:
    MainService() : running(false) {
        companyId = "";
        region = "";
        siteId = "";
        logConfig = "";
        proxyConfig = "";
        cronTab = "0 0 1 * * ?";  // Default value: Every 5 minutes
        UpgradePathManager pathManager;
        configFilePath = pathManager.GetMainConfig();
    }

    
    bool LoadConfiguration() {
        try {
            if (!CommandLineParser::LoadConfigFromFile(configFilePath, companyId, region, siteId, logConfig, proxyConfig, cronTab)) {
                spdlog::error("Failed to load configuration from file: {}", configFilePath);
                return false;
            }

            // Apply default cronTab if it's empty
            if (cronTab.empty()) {
                cronTab = "0 0 1 * * ?";
            }
            spdlog::info("Configuration loaded successfully:");
            DisplayParsedArguments();

            UpgradePathManager path;
            path.EnsureUpgradeDirectoriesExist();

            HandleConfigurationFiles();

            Logger::Init();
            return true;
        }
        catch (std::exception) {


            return false;
        }
    }

    static bool isServiceRunning(SC_HANDLE scService) {
        SERVICE_STATUS_PROCESS ssp;
        DWORD bytesNeeded;

        // Query the service status using QueryServiceStatusEx
        if (!QueryServiceStatusEx(
            scService,                      // Handle to the service
            SC_STATUS_PROCESS_INFO,         // Information level to request process information
            (LPBYTE)&ssp,                   // Structure that receives the service status
            sizeof(SERVICE_STATUS_PROCESS), // Size of the SERVICE_STATUS_PROCESS structure
            &bytesNeeded                    // Variable that receives the number of bytes needed
        )) {
            // Log an error if the service status query fails
            spdlog::error("Failed to query service status. Error code: {}", GetLastError());

            return false;  // Return false if the status query fails
        }

        // Return true if the service is running, otherwise return false
        return ssp.dwCurrentState == SERVICE_RUNNING;
    }


    static bool stopServiceByHandle(SC_HANDLE scService) {
        SERVICE_STATUS_PROCESS ssp;
        DWORD bytesNeeded;

        // Check if the service is currently running
        if (!QueryServiceStatusEx(
            scService,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp,
            sizeof(SERVICE_STATUS_PROCESS),
            &bytesNeeded
        )) {
            spdlog::error("Failed to query service status for stopping. Error code: {}", GetLastError());
            return false;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            spdlog::info("Service is already stopped.");
            return true;
        }

        // Attempting to stop the service
        SERVICE_STATUS ss;
        if (ControlService(scService, SERVICE_CONTROL_STOP, &ss)) {
            spdlog::info("Stopping service...");

            Sleep(1000);
            while (QueryServiceStatus(scService, &ss)) {
                if (ss.dwCurrentState == SERVICE_STOP_PENDING) {
                    spdlog::info("Waiting for service to stop...");
                    Sleep(1000);
                }
                else if (ss.dwCurrentState == SERVICE_STOPPED) {
                    spdlog::info("Service stopped successfully.");
                    return true;
                }
            }
        }
        else {
            spdlog::error("Failed to stop service. Error code: {}", GetLastError());
            return false;
        }
        return false;
    }

    // Check if the service is already installed
    static bool IsServiceInstalled(const std::wstring& serviceName) {
        // Open Service Control Manager
        SC_HANDLE scManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if (scManager == NULL) {
            spdlog::error("Failed to open Service Control Manager. Error code: {}", GetLastError());
            return false;
        }

        // Attempt to open the service with the specified name
        SC_HANDLE scService = OpenService(scManager, serviceName.c_str(), SERVICE_QUERY_STATUS);

        if (scService != NULL) {
            // Service opened successfully - this means it is already installed
            CloseServiceHandle(scService);
            CloseServiceHandle(scManager);
            return true;
        }
        else {
            // If unable to open the service, check if the error is due to the service not existing
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
                spdlog::info("Service does not exist: {}", ConvertWStringToString(serviceName));
            }
            else {
                spdlog::error("Failed to query service status. Error code: {}", GetLastError());
            }
        }

        // Close the SC_HANDLE and return false
        CloseServiceHandle(scManager);
        return false;  // Service is not installed or an error occurred
    }

    static void UninstallService(const std::wstring& serviceName) {
        // Check if the service is installed
        if (!IsServiceInstalled(serviceName)) {
            spdlog::warn("Service {} is not installed. Exiting...", ConvertWStringToString(serviceName));
            return;
        }

        // Open Service Control Manager
        SC_HANDLE scManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if (scManager == NULL) {
            spdlog::error("Failed to open Service Control Manager. Error code: {}", GetLastError());
            return;
        }

        // Open the service for stopping and deletion
        SC_HANDLE scService = OpenService(scManager, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
        if (scService == NULL) {
            spdlog::error("Failed to open service for deletion. Error code: {}", GetLastError());
            CloseServiceHandle(scManager);
            return;
        }

        // Check if the service is running
        if (isServiceRunning(scService)) {
            spdlog::info("Service is running, attempting to stop it...");

            // Attempt to stop the service
            if (!stopServiceByHandle(scService)) {
                spdlog::error("Failed to stop the service, cannot uninstall.");
                CloseServiceHandle(scService);
                CloseServiceHandle(scManager);
                return;
            }
        }

        // Attempt to delete the service
        if (DeleteService(scService)) {
            spdlog::info("Service {} uninstalled successfully.", ConvertWStringToString(serviceName));
        }
        else {
            spdlog::error("Failed to uninstall service {}. Error code: {}", ConvertWStringToString(serviceName), GetLastError());
        }

        // Close handles to the service and Service Control Manager
        CloseServiceHandle(scService);
        CloseServiceHandle(scManager);
    }

    
    static bool UninstallServiceSafe(const std::wstring& serviceName) {
        try {
            UninstallService(serviceName);
            return true;
            
        }
        catch (const std::exception& e) {
            spdlog::error("Exception occurred while uninstalling service '{}': {}", ConvertWStringToString(serviceName), e.what());
            return false;
        }
        catch (...) {
            spdlog::error("Unknown error occurred while uninstalling service '{}'.", ConvertWStringToString(serviceName));
            return false;
        }
    }

    static void RemoveDirectoryContents(const std::string& dirPath) {
        // Check if the directory exists and is a valid directory before trying to remove its contents
        if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
            try {
                // Iterate over all files and subdirectories in the specified directory
                for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                    try {
                        // Remove each file or subdirectory within the directory
                        std::filesystem::remove_all(entry.path());
                        spdlog::info("Removed: {}", entry.path().string());
                    }
                    catch (const std::filesystem::filesystem_error& e) {
                        spdlog::error("Error removing {}: {}", entry.path().string(), e.what());
                    }
                }
                spdlog::info("Contents of directory {} have been removed.", dirPath);
            }
            catch (const std::filesystem::filesystem_error& e) {
                spdlog::error("Error accessing directory contents: {}", e.what());
            }
        }
        else {
            spdlog::error("Directory does not exist or is not a directory: {}", dirPath);
        }
    }

    static bool CopyExeToInstallPath(const std::string& installPath) {
        char exePath[MAX_PATH];

        // Retrieve the current executable path
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) {
            spdlog::error("Could not retrieve the current executable path.");
            return false;
        }

        std::string targetExePath = installPath + "ServiceUpdater.exe";

        try {
            // Check if the target file already exists
            if (fs::exists(targetExePath)) {
                spdlog::warn("Target file already exists and will be overwritten: {}", targetExePath);
            }

            // Create the directory if it doesn't exist
            fs::create_directories(installPath);

            // Copy the executable to the target path
            fs::copy_file(exePath, targetExePath, fs::copy_options::overwrite_existing);
            spdlog::info("Successfully copied ServiceUpdater.exe to: {}", targetExePath);

            return true;
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Filesystem error during file copy: {}", e.what());
            return false;
        }
        catch (const std::exception& e) {
            spdlog::error("Error copying exe file: {}", e.what());
            return false;
        }
    }

    /**
     * @brief Starts the cron scheduler for executing scheduled tasks.
     *
     * This function initializes and runs a cron scheduler using the `cronTab` expression
     * provided in the configuration. It schedules a service upgrade task and continuously
     * checks for scheduled jobs in a loop. The scheduler remains active while `running` is true.
     *
     * If an exception occurs during the execution of a scheduled task, it logs the error.
     * If a fatal error occurs, it logs and exits the scheduler.
     */
    void StartCronScheduler() {
        try {
            spdlog::info("[Cron] Starting scheduler with expression: {}", cronTab);

            Cron cron;

            cron.add_schedule("Service Upgrade Task", cronTab, [this](auto&) {
                try {
                    spdlog::info("[Cron] Executing scheduled service upgrade...");
                    PerformServiceUpgrade();
                }
                catch (const std::exception& e) {
                    spdlog::error("[Cron] Error during service upgrade: {}", e.what());
                }
                });

            spdlog::info("[Cron] Scheduler started. Running cron jobs...");

            while (running) {
                cron.tick();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            spdlog::info("[Cron] Scheduler stopped.");
        }
        catch (const std::exception& e) {
            spdlog::error("[Cron] Fatal error: {}", e.what());
        }
    }

    /**
     * @brief Starts NexusManager: Loads configuration, performs optional installation, and starts cron jobs.
     */
    void StartNexusManager() {
        try {
            spdlog::info("[NexusManager] Starting...");

            // 1. Load Configuration FIRST (needed for installation)
            if (!LoadConfiguration()) {
                spdlog::error("[NexusManager] Failed to load configuration.");
                return;
            }

            // 2. Perform Initial Installation (if required)
            if (PerformInitialInstallation()) {
                spdlog::info("[NexusManager] Initial installation completed.");
            }
            else {
                spdlog::info("[NexusManager] Initial installation was not required.");
            }

            // 3. Start Cron Scheduler in a separate thread
            running = true;
            schedulerThread = std::thread(&MainService::StartCronScheduler, this);

            spdlog::info("[NexusManager] Successfully started.");

            // Dodaj join() kako bi se sprecilo gašenje aplikacije
            /*if (schedulerThread.joinable()) {
                schedulerThread.join();
            }*/
        }
        catch (const std::exception& e) {
            spdlog::error("[NexusManager] Exception occurred: {}", e.what());
        }
    }

    /**
     * @brief Stops NexusManager: Stops the cron scheduler and service operations.
     */
    void StopNexusManager() {
        try {
            spdlog::info("[NexusManager] Stopping...");

            running = false;  // Set the flag to stop the cron loop

            if (schedulerThread.joinable()) {
                schedulerThread.join();  // Wait for cron thread to exit
            }

            spdlog::info("[NexusManager] Stopped successfully.");
        }
        catch (const std::exception& e) {
            spdlog::error("[NexusManager] Exception while stopping: {}", e.what());
        }
    }

private:
    std::string companyId;
    std::string region;
    std::string siteId;
    std::string logConfig;
    std::string proxyConfig;
    std::string configFilePath;
    std::string cronTab;
    std::atomic<bool> running;
    std::thread schedulerThread;


    /**
     * @brief Executes the initial installation process.
     *
     * This function initializes the installation manager and performs the installation.
     * It logs relevant messages and handles any exceptions.
     *
     * @return true if the installation was successful, false otherwise.
     */
    bool PerformInitialInstallation() {
        try {
            spdlog::info("Starting Initial Installation Process...");

            // Initialize UpgradePathManager
            UpgradePathManager pathManager;

            // Initialize install manager with required parameters
            InitialInstallManager installManager(
                region, companyId, siteId,
                pathManager.GetBlobName(),
                pathManager.GetZipHashFilePath(),
                pathManager.GetServiceHashFilePath(),
                pathManager.GetZipFilePath(),
                pathManager.GetExtractedPath(),
                pathManager.GetService1Name(),
                pathManager.GetService2Name(),
                pathManager.GetService1TargetPath(),
                pathManager.GetService2TargetPath()
            );

            // Execute installation
            bool success = installManager.PerformInitialInstallation();

            if (success) {
                spdlog::info("Initial installation completed successfully.");
                return true;
            }
            else {
                spdlog::warn("Initial installation was not required or failed.");
                return false;
            }
        }
        catch (const std::exception& e) {
            spdlog::error("Exception during initial installation: {}", e.what());
            return false;
        }
    }

    /**
     * @brief Executes the service upgrade process.
     */
    void PerformServiceUpgrade() {
        try {
            spdlog::info("[Service Upgrade] Checking for service updates...");

            UpgradePathManager pathManager;

            std::string blobName = pathManager.GetBlobName();
            std::string zipHashFile = pathManager.GetZipHashFilePath();
            std::string serviceHashFile = pathManager.GetServiceHashFilePath();

            std::string downloadPath = pathManager.GetZipDirectory();
            std::string extractPath = pathManager.GetExtractedPath();

            std::wstring serviceName1 = pathManager.GetService1Name();
            std::wstring serviceName2 = pathManager.GetService2Name();

            std::wstring exePath1 = pathManager.GetService1TargetPath();
            std::wstring exePath2 = pathManager.GetService2TargetPath();

            ServiceUpgradeManager upgradeManager(
                region, companyId, siteId,
                blobName, zipHashFile, serviceHashFile,
                downloadPath, extractPath,
                serviceName1, serviceName2,
                exePath1, exePath2
            );

            if (upgradeManager.PerformUpgrade()) {
                spdlog::info("[Service Upgrade] Upgrade completed successfully.");
            }
            else {
                spdlog::info("[Service Upgrade] No updates needed.");
            }
        }
        catch (const std::exception& e) {
            spdlog::error("[Service Upgrade] Exception occurred: {}", e.what());
        }
    }

    void DisplayParsedArguments() {
        spdlog::info("Company ID: {}", companyId);
        spdlog::info("Region: {}", region);
        spdlog::info("Site ID: {}", siteId);
        spdlog::info("Log File: {}", logConfig);
        spdlog::info("Proxy Config: {}", proxyConfig);
        spdlog::info("Cron Expression: {}", cronTab);

    }   

    bool HandleConfigurationFiles() {
        UpgradePathManager pathManager;
    
        bool success = true;
    
        if (std::filesystem::exists(logConfig)) {
            if (!CommandLineParser::copy_file_robust(logConfig, pathManager.GetLoggerFilePath())) {
                spdlog::error("Failed to copy log configuration file.");
                success = false;
            }
        }
        else {
            spdlog::warn("Log configuration file does not exist, skipping.");
        }
    
        if (std::filesystem::exists(proxyConfig)) {
            if (!CommandLineParser::copy_file_robust(proxyConfig, pathManager.GetProxyFilePath())) {
                spdlog::error("Failed to copy proxy configuration file.");
                success = false;
            }
        }
        else {
            spdlog::warn("Proxy configuration file does not exist, skipping.");
        }
    
        if (success) {
            spdlog::info("Configuration files copied successfully.");
        }
    
        return success;
    }
};
#endif // MAIN_SERVICE_H
