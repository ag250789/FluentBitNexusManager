#ifndef SERVICEMANAGER_H
#define SERVICEMANAGER_H

#include "WindowsServiceManager.h"
#include "FileHasher.h"
#include "UpgradePathManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <vector>
#include <string>

namespace fs = std::filesystem;

/**
 * @class ServiceManager
 * @brief Klasa za deinstalaciju i instalaciju Windows servisa.
 */
class ServiceManager {
public:
    /**
     * @brief Konstruktor klase ServiceManager.
     * @param serviceName Ime servisa.
     * @param exePath Putanja do novog `exe` fajla servisa.
     * @param args Lista argumenata za instalaciju.
     */
    ServiceManager(const std::wstring& serviceName, const std::wstring& exePath, const std::vector<std::wstring>& args = {})
        : m_serviceName(serviceName), m_exePath(exePath), m_args(args) {
    }

    /**
     * @brief Pokre?e proces deinstalacije i instalacije servisa.
     * @return true ako je uspešno, false ako ne uspe.
     */
    bool UpdateService() {
        try {
            WindowsServiceManager manager;

            // **Proveravamo da li servis postoji i deinstaliramo ga ako je potrebno**
            if (manager.isServiceInstalled(m_serviceName)) {
                //spdlog::info("Service '{}' is already installed. Uninstalling first...", ConvertWStringToString(m_serviceName));
                LOG_INFO("Service '{}' is already installed. Uninstalling first...", ConvertWStringToString(m_serviceName));

                if (!UninstallService()) {
                    //spdlog::error("Service '{}' could not be uninstalled. Aborting update!", ConvertWStringToString(m_serviceName));
                    LOG_ERROR("Service '{}' could not be uninstalled. Aborting update!", ConvertWStringToString(m_serviceName));

                    return false;
                }

                // **Dodatna provera da li je servis stvarno uklonjen**
                if (manager.isServiceInstalled(m_serviceName)) {
                    //spdlog::error("Service '{}' is still detected as installed. Aborting installation.", ConvertWStringToString(m_serviceName));
                    LOG_ERROR("Service '{}' is still detected as installed. Aborting installation.", ConvertWStringToString(m_serviceName));

                    return false;
                }
            }

            // **Proveravamo da li novi `exe` fajl postoji**
            if (!fs::exists(m_exePath)) {
                //spdlog::error("New service executable not found: {}", ConvertWStringToString(m_exePath));
                LOG_ERROR("New service executable not found: {}", ConvertWStringToString(m_exePath));

                return false;
            }

            // ? **Pokre?emo instalaciju**
            if (!InstallService()) {
                //spdlog::error("Failed to install service '{}'.", ConvertWStringToString(m_serviceName));
                LOG_ERROR("Failed to install service '{}'.", ConvertWStringToString(m_serviceName));

                return false;
            }

            
            // ? **Brišemo `exe` fajl nakon uspešne instalacije**
            DeleteExeFile();

            UpgradePathManager pathManager;

            std::string serviceJson = pathManager.GetServiceHashFilePath();
            FileHasher hasher(serviceJson);

            std::wstring exe1 = pathManager.GetService1TargetPath();
            std::wstring exe2 = pathManager.GetService2TargetPath();

            if (m_serviceName == pathManager.GetService1Name()){
                auto exe1Hash = hasher.GetFileSHA256(exe1);
                if (exe1Hash) {
                    hasher.StoreFileHash(ConvertWStringToString(exe1), *exe1Hash);
                    //spdlog::info("Stored hash for service '{}': {}", ConvertWStringToString(exe1), *exe1Hash);
                    LOG_INFO("Stored hash for service '{}': {}", ConvertWStringToString(exe1), *exe1Hash);

                }
                else {
                    //spdlog::error("Failed to compute and store hash for '{}'", ConvertWStringToString(exe1));
                    LOG_ERROR("Failed to compute and store hash for '{}'", ConvertWStringToString(exe1));

                }
            }

            if (m_serviceName == pathManager.GetService2Name()) {
                auto exe2Hash = hasher.GetFileSHA256(exe2);
                if (exe2Hash) {
                    hasher.StoreFileHash(ConvertWStringToString(exe2), *exe2Hash);
                    //spdlog::info("Stored hash for service '{}': {}", ConvertWStringToString(exe2), *exe2Hash);
                    LOG_INFO("Stored hash for service '{}': {}", ConvertWStringToString(exe2), *exe2Hash);

                }
                else {
                    //spdlog::error("Failed to compute and store hash for '{}'", ConvertWStringToString(exe2));
                    LOG_ERROR("Failed to compute and store hash for '{}'", ConvertWStringToString(exe2));

                }
            }

            return true;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in ServiceManager: {}", e.what());
            LOG_ERROR("Exception in ServiceManager: {}", e.what());

            return false;
        }
    }

    static bool CopyServiceConfig(const std::string& sourceConfigPath, const std::string& destinationConfigPath) {
        try {
            // Provera da li konfiguracioni fajl postoji
            if (!fs::exists(sourceConfigPath)) {
                //spdlog::error("Source config file '{}' does not exist. Cannot copy.", sourceConfigPath);
                LOG_ERROR("Source config file '{}' does not exist. Cannot copy.",sourceConfigPath);
                return false;
            }

            UpgradePathManager::copy_file_robust(sourceConfigPath, destinationConfigPath);
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception while copying config for Controller Service: {}", e.what());
            LOG_ERROR("Exception while copying config for Controller Service: {}", e.what());
            return false;
        }
        catch (...) {
            //spdlog::error("Unknown error occurred while copying config.");
            LOG_ERROR("Unknown error occurred while copying config for service.");
            return false;
        }
    }


private:
    std::wstring m_serviceName;
    std::wstring m_exePath;
    std::vector<std::wstring> m_args;

    /**
     * @brief Deinstalira servis pokretanjem `uninstall` komande.
     * @return true ako je uspešno, false ako ne uspe.
     */
    bool UninstallService() {
        std::wstring uninstallCommand = L"\"" + m_exePath + L"\" uninstall";

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        if (CreateProcessW(nullptr, &uninstallCommand[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            //spdlog::info("Service '{}' uninstalled successfully.", ConvertWStringToString(m_serviceName));
            LOG_INFO("Service '{}' uninstalled successfully.", ConvertWStringToString(m_serviceName));

            return true;
        }

        //spdlog::error("Failed to uninstall service '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());
        LOG_ERROR("Failed to uninstall service '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());

        return false;
    }

    /**
     * @brief Instalira servis pokretanjem `install` komande sa argumentima.
     * @return true ako je uspešno, false ako ne uspe.
     */
    bool InstallService() {
        std::wstring installCommand = L"\"" + m_exePath + L"\" install";

        for (const auto& arg : m_args) {
            installCommand += L" " + arg;
        }

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        if (CreateProcessW(nullptr, &installCommand[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            //spdlog::info("Service '{}' installation started...", ConvertWStringToString(m_serviceName));
            LOG_INFO("Service '{}' installation started...", ConvertWStringToString(m_serviceName));

            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            //spdlog::info("Service '{}' installed successfully.", ConvertWStringToString(m_serviceName));
            LOG_INFO("Service '{}' installed successfully.", ConvertWStringToString(m_serviceName));

            return true;
        }

        //spdlog::error("Failed to start installation process for '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());
        LOG_ERROR("Failed to start installation process for '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());

        return false;
    }

    /**
     * @brief Briše `exe` fajl nakon uspešne instalacije.
     */
    void DeleteExeFile() {
        if (fs::exists(m_exePath)) {
            try {
                fs::remove(m_exePath);
                //spdlog::info("Deleted temporary service executable: {}", ConvertWStringToString(m_exePath));
                LOG_INFO("Deleted temporary service executable: {}", ConvertWStringToString(m_exePath));

            }
            catch (const std::exception& e) {
                //spdlog::warn("Failed to delete service executable '{}': {}", ConvertWStringToString(m_exePath), e.what());
                LOG_WARN("Failed to delete service executable '{}': {}", ConvertWStringToString(m_exePath), e.what());

            }
        }
    }
};

#endif // SERVICEMANAGER_H
