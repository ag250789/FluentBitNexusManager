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

    bool ShouldPerformInitialInstall() {
        std::string configPath = m_extractPath + "\\ncrv_dcs_streaming_service_upgrade_manager\\install_config.json";

        // Ako `install_config.json` ne postoji, radimo inicijalnu instalaciju samo ako servisi nisu instalirani
        if (!fs::exists(configPath)) {
            //spdlog::warn("`install_config.json` not found. Proceeding with initial install check.");
            LOG_WARN("`install_config.json` not found. Proceeding with initial install check.");

            return !AreServicesInstalled();  // Ako servisi nisu instalirani, instaliramo ih.
        }

        try {
            std::ifstream configFile(configPath);
            if (!configFile.is_open()) {
                //spdlog::error("Failed to open `install_config.json`.");
                LOG_ERROR("Failed to open `install_config.json`.");

                return false;
            }

            json config;
            configFile >> config;
            configFile.close();

            // ? Logovanje dodatnih podataka
            if (config.contains("install_reason") && config["install_reason"].is_string()) {
                //spdlog::info("Initial Install reason: {}", config["install_reason"].get<std::string>());
                LOG_INFO("Initial Install reason: {}", config["install_reason"].get<std::string>());

            }

            if (config.contains("required_version") && config["required_version"].is_string()) {
                //spdlog::info("Required version: {}", config["required_version"].get<std::string>());
                LOG_INFO("Required version: {}", config["required_version"].get<std::string>());

            }

            if (config.contains("timestamp") && config["timestamp"].is_string()) {
                //spdlog::info("Install timestamp: {}", config["timestamp"].get<std::string>());
                LOG_INFO("Install timestamp: {}", config["timestamp"].get<std::string>());

            }

            // ? Logovanje liste servisa koji ?e biti instalirani
            if (config.contains("services") && config["services"].is_array()) {
                //spdlog::info("Services to install:");
                LOG_INFO("Services to install:");

                for (const auto& service : config["services"]) {
                    if (service.contains("name") && service.contains("exe")) {
                        /*spdlog::info("   --> Service: {}, Executable: {}",
                            service["name"].get<std::string>(),
                            service["exe"].get<std::string>());*/
                        LOG_INFO("   --> Service: {}, Executable: {}",
                            service["name"].get<std::string>(),
                            service["exe"].get<std::string>());
                    }
                }
            }
            else {
                //spdlog::warn("No services defined in `install_config.json`.");
                LOG_WARN("No services defined in `install_config.json`.");

            }

            bool enableInstall = config.contains("enable_initial_install") && config["enable_initial_install"].is_boolean()
                ? config["enable_initial_install"].get<bool>()
                : false;

            if (!enableInstall && !AreServicesInstalled()) {
                //spdlog::warn("`install_config.json` disables installation, but services are missing. Proceeding with install.");
                LOG_WARN("`install_config.json` disables installation, but services are missing. Proceeding with install.");

                return true;
            }


            //spdlog::info("`install_config.json` found. Initial install enabled: {}", enableInstall ? "YES" : "NO");
            LOG_INFO("`install_config.json` found. Initial install enabled: {}", enableInstall ? "YES" : "NO");

            return enableInstall;
        }
        catch (const std::exception& e) {
            //spdlog::error("Error reading `install_config.json`: {}", e.what());
            LOG_ERROR("Error reading `install_config.json`: {}", e.what());

            return false;
        }
    }
    /**
     * @brief Pokre?e proces inicijalne instalacije servisa.
     * @return true ako je instalacija uspešna, false ako nije potrebna ili ako do?e do greške.
     */
    bool PerformInitialInstallation() {
        try {

            //spdlog::info("Starting initial service installation process...");
            LOG_INFO("Starting initial service installation process...");


            

            // 1?? Preuzmi i raspakuj ZIP
            if (!m_updateManager.PerformInitialInstallation()) {
                //spdlog::info("No initial installation needed.");
                LOG_INFO("No initial installation needed.");

                return false;
            }

            // Proveravamo da li treba da radimo inicijalnu instalaciju
            if (!ShouldPerformInitialInstall()) {
                //spdlog::warn("Initial installation skipped based on `install_config.json`.");
                LOG_WARN("Initial installation skipped based on `install_config.json`.");
                m_updateManager.CleanExtractedFolder();

                return false;
            }

            // 2?? Instaliraj servise ako nisu ve? instalirani
            bool installationPerformed = false;
            for (const auto& [serviceName, exePath, newExeName] : m_services) {
                if (InstallServiceIfNeeded(newExeName, serviceName)) {
                    installationPerformed = true;
                }
            }

            if (installationPerformed) {
                //spdlog::info("Initial service installation completed successfully.");
                LOG_INFO("Initial service installation completed successfully.");

                m_updateManager.CleanExtractedFolder();
            }
            else {
                //spdlog::info("[InitialInstallManager] No services required installation.");
                LOG_INFO("No services required installation.");

            }

            return installationPerformed;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in PerformInitialInstallation: {}", e.what());
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
     * @brief Proverava da li su servisi ve? instalirani.
     * @return `true` ako su svi servisi instalirani, `false` ako bar jedan nedostaje.
     */
    bool AreServicesInstalled() {
        WindowsServiceManager serviceManager;

        for (const auto& [serviceName, exePath, newExeName] : m_services) {
            if (!serviceManager.isServiceInstalled(serviceName)) {
                //spdlog::warn("Service '{}' is not installed.", ConvertWStringToString(serviceName));
                LOG_WARN("Service '{}' is not installed.", ConvertWStringToString(serviceName));

                return false;
            }
        }

        //spdlog::info("All services are already installed.");
        LOG_INFO("All services are already installed.");

        return true;
    }

    /**
     * @brief Instalira servis ako ve? nije instaliran.
     * @return true ako je servis instaliran, false ako je ve? postojao.
     */
    bool InstallServiceIfNeeded(const std::wstring& newExeName, const std::wstring& serviceName) {
        std::wstring newExePath = fs::path(m_extractPath) / L"ncrv_dcs_streaming_service_upgrade_manager" / newExeName;

        if (!fs::exists(newExePath)) {
            //spdlog::warn("New executable does not exist: {}", ConvertWStringToString(newExePath));
            LOG_WARN("New executable does not exist: {}", ConvertWStringToString(newExePath));

            return false;
        }

        //spdlog::info("Installing service '{}'", ConvertWStringToString(serviceName));
        LOG_INFO("Installing service '{}'", ConvertWStringToString(serviceName));


        std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

        ServiceManager serviceManager(serviceName, newExePath, args);
        return serviceManager.UpdateService();
    }


    /**
     * @brief Generiše argumente za instalaciju servisa (osim za Watchdog koji ih nema).
     * @return Lista argumenata.
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
