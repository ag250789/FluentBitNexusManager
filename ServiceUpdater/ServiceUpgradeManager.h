#ifndef SERVICEUPGRADEMANAGER_H
#define SERVICEUPGRADEMANAGER_H

#include "UpdateManager.h"
#include "ServiceRestartManager.h"
#include "FileHasher.h"
#include "ServiceManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

class ServiceUpgradeManager {
public:
    ServiceUpgradeManager(const std::string& region, const std::string& customerId, const std::string& siteId,
        const std::string& blobName, const std::string& zipHashFile, const std::string& serviceHashFile,
        const std::string& downloadPath, const std::string& extractPath,
        const std::wstring& serviceName1, const std::wstring& serviceName2,
        const std::wstring& exePath1, const std::wstring& exePath2)
        : m_updateManager(region, customerId, siteId, blobName, zipHashFile, downloadPath + "\\" + blobName, extractPath),
        m_zipFileHasher(zipHashFile),     
        m_serviceFileHasher(serviceHashFile), 
        m_zipHashFile(zipHashFile),
        m_serviceHashFile(serviceHashFile),
        m_services{ {serviceName1, exePath1, L"FluentBitManager.exe"},
                   {serviceName2, exePath2, L"WatchdogFluentBit.exe"} },
        m_downloadPath(downloadPath),
        m_extractPath(extractPath),
        m_region(ConvertStringToWString(region)),
        m_customerId(ConvertStringToWString(customerId)),
        m_siteId(ConvertStringToWString(siteId)),
        m_fullReinstall(false)  
    {
    }

    /**
     * @brief Performs the service upgrade process.
     *
     * This function initiates the upgrade by checking for updates via `UpdateManager`.
     * If a ZIP file update is detected, it verifies whether a full reinstall is required.
     * It then compares the current service executables with the new ones and updates them if needed.
     * After a successful upgrade, it cleans up extracted files.
     *
     * @return true if the upgrade was successful, false otherwise.
     */
    bool PerformUpgrade() {
        try {
            LOG_INFO("Starting service upgrade process...");

            if (!m_updateManager.PerformUpdate()) {
                LOG_INFO("No ZIP update necessary.");

                return false;
            }

            m_fullReinstall = m_updateManager.NeedsFullReinstall();

            bool updatePerformed = false;
            for (const auto& [serviceName, exePath, newExeName] : m_services) {
                if (CompareAndUpdateService(exePath, newExeName, serviceName)) {
                    updatePerformed = true;
                }
            }

            if (updatePerformed) {
                LOG_INFO("Service upgrade completed successfully.");

                m_updateManager.CleanExtractedFolder();
            }
            else {
                LOG_INFO("No services required updating.");

            }

            return updatePerformed;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in PerformUpgrade: {}", e.what());

            return false;
        }
    }

private:
    struct ServiceInfo {
        std::wstring serviceName;  ///< The name of the service.
        std::wstring exePath;      ///< The current executable path of the service.
        std::wstring newExeName;   ///< The new executable name after the update.
    };

    UpdateManager m_updateManager;     ///< Manages downloading and extracting updates.
    FileHasher m_zipFileHasher;        ///< Tracks the hash values of ZIP update files.
    FileHasher m_serviceFileHasher;    ///< Tracks the hash values of service executables.
    std::string m_zipHashFile;         ///< Path to the JSON file storing ZIP hash values.
    std::string m_serviceHashFile;     ///< Path to the JSON file storing service hash values.
    std::vector<ServiceInfo> m_services;  ///< List of services being managed.
    std::string m_downloadPath;        ///< Path where update files are downloaded.
    std::string m_extractPath;         ///< Path where update files are extracted.
    std::wstring m_region;             ///< The region associated with the service.
    std::wstring m_customerId;         ///< The customer ID for service management.
    std::wstring m_siteId;             ///< The site ID for service management.
    bool m_fullReinstall;              ///< Indicates whether a full reinstall is needed.


    /**
     * @brief Generates a list of arguments for service installation.
     *
     * This function constructs a list of command-line arguments that will be passed to
     * the service during installation. The arguments include company ID, region, and site ID
     * if they are available.
     *
     * @return A vector containing installation arguments. If no arguments are needed, the vector is empty.
     */
    std::vector<std::wstring> GenerateServiceArguments() {
        std::vector<std::wstring> args;

        if (!m_customerId.empty()) {
            args.push_back(L"--companyid");
            args.push_back(m_customerId);
        }

        if (!m_region.empty()) {
            args.push_back(L"--region");
            args.push_back(m_region);
        }

        if (!m_siteId.empty()) {
            args.push_back(L"--siteid");
            args.push_back(m_siteId);
        }

        return args;
    }
    
    /**
     * @brief Compares an existing service executable with a new one and updates if necessary.
     *
     * This function checks if the new executable file exists and compares its hash with the
     * currently installed version. If the files differ, it updates the service and restarts it.
     *
     * - If the target executable is missing and a full reinstall is required, the service is reinstalled.
     * - If the executable has changed, the service is updated and restarted.
     * - If no update is needed, the function logs the status and exits.
     *
     * @param targetExePath The path of the currently installed service executable.
     * @param newExeName The name of the new executable file.
     * @param serviceName The name of the service to be updated.
     * @return true if the service was updated, false if no update was needed or if an error occurred.
     */
    bool CompareAndUpdateService(const std::wstring& targetExePath, const std::wstring& newExeName,
        const std::wstring& serviceName) {
        //std::wstring newExePath = fs::path(m_extractPath) / newExeName;
        std::wstring newExePath = fs::path(m_extractPath) / L"ncrv_dcs_streaming_service_upgrade_manager" / newExeName;


        if (!fs::exists(newExePath)) {
            LOG_WARN("New executable does not exist: {}", ConvertWStringToString(newExePath));

            return false;
        }

        if (!fs::exists(targetExePath)) {
            LOG_WARN("Target executable '{}' does not exist!", ConvertWStringToString(targetExePath));

            if (m_fullReinstall) {
                LOG_INFO("Full reinstall required for '{}' as the target exe is missing.", ConvertWStringToString(serviceName));

                std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

                ServiceManager serviceManager(serviceName, newExePath, args);
                return serviceManager.UpdateService();
            }
            else {
                LOG_ERROR("Target executable '{}' is missing, but full reinstall is not enabled! Aborting update.", ConvertWStringToString(serviceName));

                return true;
            }
        }

        if (!m_serviceFileHasher.CheckAndUpdateFileHash(ConvertWStringToString(targetExePath),
            ConvertWStringToString(newExePath),
            m_serviceHashFile)) {
            LOG_INFO("No update required for '{}'.", ConvertWStringToString(targetExePath));

            return false;
        }

        
        LOG_INFO("Executable '{}' has changed. Updating and restarting service '{}'.",
            ConvertWStringToString(targetExePath), ConvertWStringToString(serviceName));
        if (m_fullReinstall) {
            LOG_INFO("Full reinstall required for '{}'", ConvertWStringToString(serviceName));

            std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

            ServiceManager serviceManager(serviceName, newExePath, args);
            return serviceManager.UpdateService();
        }
        else {
            LOG_INFO("Restarting service '{}'", ConvertWStringToString(serviceName));

            ServiceRestartManager serviceManager(serviceName, newExePath, targetExePath);
            return serviceManager.UpdateAndRestartService();
        }

        
    }
};

#endif // SERVICEUPGRADEMANAGER_H
