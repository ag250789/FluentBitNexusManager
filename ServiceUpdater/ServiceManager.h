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
 * @brief Manages the installation and uninstallation of Windows services.
 *
 * This class provides functionality to uninstall an existing service, install a new one,
 * and manage related service configuration files. It ensures that the service is correctly
 * installed and that outdated executable files are deleted after installation.
 */
class ServiceManager {
public:
    /**
     * @brief Constructs a ServiceManager instance.
     *
     * Initializes the service manager with the given service name, executable path,
     * and optional installation arguments.
     *
     * @param serviceName The name of the service to be managed.
     * @param exePath The path to the service's executable file.
     * @param args A list of arguments for service installation.
     */
    ServiceManager(const std::wstring& serviceName, const std::wstring& exePath, const std::vector<std::wstring>& args = {})
        : m_serviceName(serviceName), m_exePath(exePath), m_args(args) {
    }

    /**
     * @brief Uninstalls an existing service (if installed) and installs a new version.
     *
     * This function first checks whether the service is already installed. If it is,
     * the service is uninstalled before proceeding with the installation of the new
     * executable. It also verifies that the executable file exists before installation.
     *
     * @return true if the update is successful, false otherwise.
     */
    bool UpdateService() {
        try {
            WindowsServiceManager manager;

            if (manager.isServiceInstalled(m_serviceName)) {
                LOG_INFO("Service '{}' is already installed. Uninstalling first...", ConvertWStringToString(m_serviceName));

                if (!UninstallService()) {
                    LOG_ERROR("Service '{}' could not be uninstalled. Aborting update!", ConvertWStringToString(m_serviceName));

                    return false;
                }

                if (manager.isServiceInstalled(m_serviceName)) {
                    LOG_ERROR("Service '{}' is still detected as installed. Aborting installation.", ConvertWStringToString(m_serviceName));

                    return false;
                }
            }

            if (!fs::exists(m_exePath)) {
                LOG_ERROR("New service executable not found: {}", ConvertWStringToString(m_exePath));

                return false;
            }

            if (!InstallService()) {
                LOG_ERROR("Failed to install service '{}'.", ConvertWStringToString(m_serviceName));

                return false;
            }
            
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
                    LOG_INFO("Stored hash for service '{}': {}", ConvertWStringToString(exe1), *exe1Hash);

                }
                else {
                    LOG_ERROR("Failed to compute and store hash for '{}'", ConvertWStringToString(exe1));

                }
            }

            if (m_serviceName == pathManager.GetService2Name()) {
                auto exe2Hash = hasher.GetFileSHA256(exe2);
                if (exe2Hash) {
                    hasher.StoreFileHash(ConvertWStringToString(exe2), *exe2Hash);
                    LOG_INFO("Stored hash for service '{}': {}", ConvertWStringToString(exe2), *exe2Hash);

                }
                else {
                    LOG_ERROR("Failed to compute and store hash for '{}'", ConvertWStringToString(exe2));

                }
            }

            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in ServiceManager: {}", e.what());

            return false;
        }
    }

    /**
     * @brief Copies a service configuration file to a new location.
     *
     * This function ensures that the configuration file exists before attempting
     * to copy it to the destination.
     *
     * @param sourceConfigPath The path to the source configuration file.
     * @param destinationConfigPath The destination path where the configuration file should be copied.
     * @return true if the file is copied successfully, false otherwise.
     */

    static bool CopyServiceConfig(const std::string& sourceConfigPath, const std::string& destinationConfigPath) {
        try {
            if (!fs::exists(sourceConfigPath)) {
                LOG_ERROR("Source config file '{}' does not exist. Cannot copy.",sourceConfigPath);
                return false;
            }

            UpgradePathManager::copy_file_robust(sourceConfigPath, destinationConfigPath);
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception while copying config for Controller Service: {}", e.what());
            return false;
        }
        catch (...) {
            LOG_ERROR("Unknown error occurred while copying config for service.");
            return false;
        }
    }


private:
    std::wstring m_serviceName;  ///< The name of the service being managed.
    std::wstring m_exePath;      ///< The path to the service executable.
    std::vector<std::wstring> m_args;  ///< The list of installation arguments.

    /**
     * @brief Uninstalls the service using the `uninstall` command.
     *
     * This function runs the service's executable with the `uninstall` command to remove the service.
     *
     * @return true if the service was successfully uninstalled, false otherwise.
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
            LOG_INFO("Service '{}' uninstalled successfully.", ConvertWStringToString(m_serviceName));

            return true;
        }

        LOG_ERROR("Failed to uninstall service '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());

        return false;
    }

    /**
     * @brief Installs the service using the `install` command.
     *
     * This function executes the service's installation command along with any provided arguments.
     *
     * @return true if the service was successfully installed, false otherwise.
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
            LOG_INFO("Service '{}' installation started...", ConvertWStringToString(m_serviceName));

            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            LOG_INFO("Service '{}' installed successfully.", ConvertWStringToString(m_serviceName));

            return true;
        }

        LOG_ERROR("Failed to start installation process for '{}'. Error: {}", ConvertWStringToString(m_serviceName), GetLastError());

        return false;
    }

    /**
     * @brief Deletes the service's executable file after installation.
     *
     * This function ensures that the executable file is removed once the installation
     * process is complete, preventing unnecessary file retention.
     */
    void DeleteExeFile() {
        if (fs::exists(m_exePath)) {
            try {
                fs::remove(m_exePath);
                LOG_INFO("Deleted temporary service executable: {}", ConvertWStringToString(m_exePath));

            }
            catch (const std::exception& e) {
                LOG_WARN("Failed to delete service executable '{}': {}", ConvertWStringToString(m_exePath), e.what());

            }
        }
    }
};

#endif // SERVICEMANAGER_H
