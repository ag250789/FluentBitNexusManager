#ifndef INITIALINSTALLMANAGER_H
#define INITIALINSTALLMANAGER_H

#include "UpdateManager.h"
#include "ServiceManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

class InitialInstallManager {
public:
    InitialInstallManager(const std::string& region, const std::string& customerId, const std::string& siteId,
        const std::string& blobName, const std::string& zipHashFile, const std::string& serviceHashFile,
        const std::string& downloadPath, const std::string& extractPath,
        const std::wstring& serviceName1, const std::wstring& serviceName2,
        const std::wstring& exePath1, const std::wstring& exePath2)
        : m_updateManager(region, customerId, siteId, blobName, zipHashFile, downloadPath, extractPath),
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
        m_siteId(ConvertStringToWString(siteId)) {
    }

    /**
     * @brief Determines whether the initial installation process should be performed.
     *
     * This function checks for the existence of `install_config.json` and evaluates its contents
     * to decide whether an initial installation is required. If the configuration file does not
     * exist, it checks if services are already installed. It also logs details about the installation
     * reason, required version, timestamp, and services to be installed.
     *
     * @return true if the initial installation should proceed, false otherwise.
     */
    bool ShouldPerformInitialInstall() {
        std::string configPath = m_extractPath + "\\ncrv_dcs_streaming_service_upgrade_manager\\install_config.json";

        if (!fs::exists(configPath)) {
            LOG_WARN("`install_config.json` not found. Proceeding with initial install check.");

            return !AreServicesInstalled();  
        }

        try {
            std::ifstream configFile(configPath);
            if (!configFile.is_open()) {
                LOG_ERROR("Failed to open `install_config.json`.");

                return false;
            }

            json config;
            configFile >> config;
            configFile.close();

            if (config.contains("install_reason") && config["install_reason"].is_string()) {
                LOG_INFO("Initial Install reason: {}", config["install_reason"].get<std::string>());

            }

            if (config.contains("required_version") && config["required_version"].is_string()) {
                LOG_INFO("Required version: {}", config["required_version"].get<std::string>());

            }

            if (config.contains("timestamp") && config["timestamp"].is_string()) {
                LOG_INFO("Install timestamp: {}", config["timestamp"].get<std::string>());

            }

            if (config.contains("services") && config["services"].is_array()) {
                LOG_INFO("Services to install:");

                for (const auto& service : config["services"]) {
                    if (service.contains("name") && service.contains("exe")) {
                        
                        LOG_INFO("   --> Service: {}, Executable: {}",
                            service["name"].get<std::string>(),
                            service["exe"].get<std::string>());
                    }
                }
            }
            else {
                LOG_WARN("No services defined in `install_config.json`.");

            }

            bool enableInstall = config.contains("enable_initial_install") && config["enable_initial_install"].is_boolean()
                ? config["enable_initial_install"].get<bool>()
                : false;

            if (!enableInstall && !AreServicesInstalled()) {
                LOG_WARN("`install_config.json` disables installation, but services are missing. Proceeding with install.");

                return true;
            }


            LOG_INFO("`install_config.json` found. Initial install enabled: {}", enableInstall ? "YES" : "NO");

            return enableInstall;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error reading `install_config.json`: {}", e.what());

            return false;
        }
    }
    
    /**
     * @brief Initiates the initial service installation process.
     *
     * This function performs the initial installation by extracting necessary files, verifying
     * installation conditions, and installing required services. If no services require installation,
     * the function logs the event and exits. It also cleans up extracted files after installation
     * is completed.
     *
     * @return true if the installation was successful, false otherwise.
     */
    bool PerformInitialInstallation() {
        try {

            LOG_INFO("Starting initial service installation process...");

            if (!m_updateManager.PerformInitialInstallation()) {
                LOG_INFO("No initial installation needed.");

                return false;
            }

            if (!ShouldPerformInitialInstall()) {
                LOG_WARN("Initial installation skipped based on `install_config.json`.");
                m_updateManager.CleanExtractedFolder();

                return false;
            }

            bool installationPerformed = false;
            for (const auto& [serviceName, exePath, newExeName] : m_services) {
                if (InstallServiceIfNeeded(newExeName, serviceName)) {
                    installationPerformed = true;
                }
            }

            if (installationPerformed) {
                LOG_INFO("Initial service installation completed successfully.");

                m_updateManager.CleanExtractedFolder();
            }
            else {
                LOG_INFO("No services required installation.");

            }

            return installationPerformed;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in PerformInitialInstallation: {}", e.what());

            return false;
        }
    }

private:
    struct ServiceInfo {
        std::wstring serviceName;
        std::wstring exePath;
        std::wstring newExeName;
    };

    UpdateManager m_updateManager;
    FileHasher m_zipFileHasher;
    FileHasher m_serviceFileHasher;
    std::string m_zipHashFile;
    std::string m_serviceHashFile;
    std::vector<ServiceInfo> m_services;
    std::string m_downloadPath;
    std::string m_extractPath;
    std::wstring m_region;
    std::wstring m_customerId;
    std::wstring m_siteId;

    /**
     * @brief Checks if all required services are installed.
     *
     * This function iterates through the list of services and verifies if each one is installed
     * using the Windows Service Manager. If any service is missing, it logs a warning and returns `false`.
     *
     * @return true if all services are installed, false otherwise.
     */
    bool AreServicesInstalled() {
        WindowsServiceManager serviceManager;

        for (const auto& [serviceName, exePath, newExeName] : m_services) {
            if (!serviceManager.isServiceInstalled(serviceName)) {
                LOG_WARN("Service '{}' is not installed.", ConvertWStringToString(serviceName));

                return false;
            }
        }

        LOG_INFO("All services are already installed.");

        return true;
    }

    /**
     * @brief Installs a service if it is not already installed.
     *
     * This function checks if the service's executable exists in the extraction directory. If the
     * file is present, it attempts to install or update the service. If the service is already installed,
     * it returns `false`.
     *
     * @param newExeName The name of the new executable file for the service.
     * @param serviceName The name of the service to install.
     * @return true if the service was installed successfully, false if it was already present.
     */
    bool InstallServiceIfNeeded(const std::wstring& newExeName, const std::wstring& serviceName) {
        std::wstring newExePath = fs::path(m_extractPath) / L"ncrv_dcs_streaming_service_upgrade_manager" / newExeName;

        if (!fs::exists(newExePath)) {
            LOG_WARN("New executable does not exist: {}", ConvertWStringToString(newExePath));

            return false;
        }

        LOG_INFO("Installing service '{}'", ConvertWStringToString(serviceName));


        std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

        ServiceManager serviceManager(serviceName, newExePath, args);
        return serviceManager.UpdateService();
    }


    /**
     * @brief Generates command-line arguments for service installation.
     *
     * This function constructs a list of arguments for the service installation command,
     * including company ID, region, and site ID. The Watchdog service does not require arguments.
     *
     * @return A list of arguments as `std::vector<std::wstring>`.
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
};

#endif // INITIALINSTALLMANAGER_H
