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
        m_zipFileHasher(zipHashFile),     // Pracenje hash vrednosti ZIP fajla
        m_serviceFileHasher(serviceHashFile), // Pracenje hash vrednosti executable servisa
        m_zipHashFile(zipHashFile),
        m_serviceHashFile(serviceHashFile),
        m_services{ {serviceName1, exePath1, L"FluentBitManager.exe"},
                   {serviceName2, exePath2, L"WatchdogFluentBit.exe"} },
        m_downloadPath(downloadPath),
        m_extractPath(extractPath),
        m_region(ConvertStringToWString(region)),
        m_customerId(ConvertStringToWString(customerId)),
        m_siteId(ConvertStringToWString(siteId)),
        m_fullReinstall(false)  // Podrazumevano radimo restart, osim ako JSON kaže drugacije
    {
    }

    /**
     * @brief Pokre?e proces nadogradnje servisa.
     * @return true ako je nadogradnja uspešna, false ako nije potrebna ili ako je došlo do greške.
     */
    bool PerformUpgrade() {
        try {
            //spdlog::info("[ServiceUpgradeManager] Starting service upgrade process...");
            LOG_INFO("Starting service upgrade process...");

            // 1. Pokreni UpdateManager za preuzimanje i proveru ZIP fajla
            if (!m_updateManager.PerformUpdate()) {
                //spdlog::info("[ServiceUpgradeManager] No ZIP update necessary.");
                LOG_INFO("No ZIP update necessary.");

                return false;
            }

            // **Koristimo funkciju iz `UpdateManager` da proverimo da li radimo full reinstall!**
            m_fullReinstall = m_updateManager.NeedsFullReinstall();

            // 2. Uporedi i ažuriraj svaki servis
            bool updatePerformed = false;
            for (const auto& [serviceName, exePath, newExeName] : m_services) {
                if (CompareAndUpdateService(exePath, newExeName, serviceName)) {
                    updatePerformed = true;
                }
            }

            if (updatePerformed) {
                //spdlog::info("[ServiceUpgradeManager] Service upgrade completed successfully.");
                LOG_INFO("Service upgrade completed successfully.");

                m_updateManager.CleanExtractedFolder();
            }
            else {
                //spdlog::info("[ServiceUpgradeManager] No services required updating.");
                LOG_INFO("No services required updating.");

            }

            return updatePerformed;
        }
        catch (const std::exception& e) {
            //spdlog::error("[ServiceUpgradeManager] Exception in PerformUpgrade: {}", e.what());
            LOG_ERROR("Exception in PerformUpgrade: {}", e.what());

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
    FileHasher m_zipFileHasher;       // ? Pra?enje hash vrednosti ZIP fajla
    FileHasher m_serviceFileHasher;   // ? Pra?enje hash vrednosti executable servisa
    std::string m_zipHashFile;        // ? JSON za ZIP fajlove
    std::string m_serviceHashFile;    // ? JSON za servise (exe)
    std::vector<ServiceInfo> m_services;
    std::string m_downloadPath;
    std::string m_extractPath;
    std::wstring m_region;
    std::wstring m_customerId;
    std::wstring m_siteId;
    bool m_fullReinstall; // ? Promenljiva koja odre?uje da li je potreban full reinstall


    /**
     * @brief Generiše listu argumenata za instalaciju servisa.
     * @return Vektor argumenata (ili prazan ako ih nema).
     */
    std::vector<std::wstring> GenerateServiceArguments() {
        std::vector<std::wstring> args;

        // ? Ako imamo `m_customerId`, dodaj `--companyid`
        if (!m_customerId.empty()) {
            args.push_back(L"--companyid");
            args.push_back(m_customerId);
        }

        // ? Ako postoji `m_region`, dodaj `--region`
        if (!m_region.empty()) {
            args.push_back(L"--region");
            args.push_back(m_region);
        }

        // ? Ako postoji `m_siteId`, dodaj `--siteid`
        if (!m_siteId.empty()) {
            args.push_back(L"--siteid");
            args.push_back(m_siteId);
        }

        return args;
    }
    /**
     * @brief Proverava da li se exe fajl promenio i ako jeste, restartuje servis.
     * @return true ako je servis ažuriran, false ako nije.
     */
    bool CompareAndUpdateService(const std::wstring& targetExePath, const std::wstring& newExeName,
        const std::wstring& serviceName) {
        //std::wstring newExePath = fs::path(m_extractPath) / newExeName;
        std::wstring newExePath = fs::path(m_extractPath) / L"ncrv_dcs_streaming_service_upgrade_manager" / newExeName;


        if (!fs::exists(newExePath)) {
            //spdlog::warn("[ServiceUpgradeManager] New executable does not exist: {}", ConvertWStringToString(newExePath));
            LOG_WARN("New executable does not exist: {}", ConvertWStringToString(newExePath));

            return false;
        }

        if (!fs::exists(targetExePath)) {
            //spdlog::warn("[ServiceUpgradeManager] Target executable '{}' does not exist!", ConvertWStringToString(targetExePath));
            LOG_WARN("Target executable '{}' does not exist!", ConvertWStringToString(targetExePath));

            if (m_fullReinstall) {
                //spdlog::info("[ServiceUpgradeManager] Full reinstall required for '{}' as the target exe is missing.", ConvertWStringToString(serviceName));
                LOG_INFO("Full reinstall required for '{}' as the target exe is missing.", ConvertWStringToString(serviceName));

                // **Ako je Watchdog, ne šaljemo argumente**
                std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

                ServiceManager serviceManager(serviceName, newExePath, args);
                return serviceManager.UpdateService();
            }
            else {
                //spdlog::error("[ServiceUpgradeManager] ? Target executable '{}' is missing, but full reinstall is not enabled! Aborting update.", ConvertWStringToString(serviceName));
                LOG_ERROR("Target executable '{}' is missing, but full reinstall is not enabled! Aborting update.", ConvertWStringToString(serviceName));

                return true;
            }
        }

        // Koristi odvojeni JSON za hashove servisa
        if (!m_serviceFileHasher.CheckAndUpdateFileHash(ConvertWStringToString(targetExePath),
            ConvertWStringToString(newExePath),
            m_serviceHashFile)) {
            //spdlog::info("[ServiceUpgradeManager] No update required for '{}'.", ConvertWStringToString(targetExePath));
            LOG_INFO("No update required for '{}'.", ConvertWStringToString(targetExePath));

            return false;
        }

        /*spdlog::info("[ServiceUpgradeManager] Executable '{}' has changed. Updating and restarting service '{}'.",
            ConvertWStringToString(targetExePath), ConvertWStringToString(serviceName));*/

        LOG_INFO("Executable '{}' has changed. Updating and restarting service '{}'.",
            ConvertWStringToString(targetExePath), ConvertWStringToString(serviceName));
        // Ako je potreban full reinstall
        if (m_fullReinstall) {
            //spdlog::info("Full reinstall required for '{}'", ConvertWStringToString(serviceName));
            LOG_INFO("Full reinstall required for '{}'", ConvertWStringToString(serviceName));

            //std::vector<std::wstring> args = GenerateServiceArguments();
            std::vector<std::wstring> args = (serviceName == L"DCSStreamingAgentWatchdog") ? std::vector<std::wstring>{} : GenerateServiceArguments();

            // Koristimo ServiceManager za reinstalaciju
            ServiceManager serviceManager(serviceName, newExePath, args);
            return serviceManager.UpdateService();
        }
        else {
            //spdlog::info("Restarting service '{}'", ConvertWStringToString(serviceName));
            LOG_INFO("Restarting service '{}'", ConvertWStringToString(serviceName));

            // ? Koristimo ServiceRestartManager za restart
            ServiceRestartManager serviceManager(serviceName, newExePath, targetExePath);
            return serviceManager.UpdateAndRestartService();
        }

        
    }
};

#endif // SERVICEUPGRADEMANAGER_H
