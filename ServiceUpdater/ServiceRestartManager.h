#ifndef SERVICERESTARTMANAGER_H
#define SERVICERESTARTMANAGER_H

#include "WindowsServiceManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <Windows.h>

namespace fs = std::filesystem;

class ServiceRestartManager {
public:
    ServiceRestartManager(const std::wstring& serviceName,
        const std::wstring& newFilePath,
        const std::wstring& targetPath)
        : m_serviceName(serviceName),
        m_newFilePath(newFilePath),
        m_targetPath(targetPath) {
    }

    /**
     * @brief Updates a service by replacing its executable and restarting it.
     *
     * This function checks if the service is installed, stops it if it is running,
     * creates a backup of the existing executable, copies the new executable to the target path,
     * and attempts to restart the service. If the update fails, a rollback mechanism restores
     * the previous executable and attempts to restart the service.
     *
     * @return true if the update and restart were successful, false otherwise.
     */
    bool UpdateAndRestartService() {
        WindowsServiceManager serviceManager;

        LOG_INFO("Attempting to update and restart service '{}'", ConvertWStringToString(m_serviceName));

        if (!serviceManager.isServiceInstalled(m_serviceName)) {
            LOG_ERROR("Service '{}' is not installed.", ConvertWStringToString(m_serviceName));

            return false;
        }

        LOG_INFO("Service '{}' is installed.", ConvertWStringToString(m_serviceName));

        if (!fs::exists(m_newFilePath)) {
            LOG_ERROR("New file does not exist: {}", ConvertWStringToString(m_newFilePath));

            return false;
        }

        LOG_INFO("New file found at '{}'", ConvertWStringToString(m_newFilePath));

        bool wasServiceRunning = serviceManager.isServiceRunning(m_serviceName);

        if (wasServiceRunning) {
            LOG_INFO("Stopping service: {}", ConvertWStringToString(m_serviceName));

            serviceManager.stopService(m_serviceName);

            int retries = 10;
            while (serviceManager.isServiceRunning(m_serviceName) && retries-- > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                LOG_WARN("Waiting for service '{}' to stop... ({} retries left)", ConvertWStringToString(m_serviceName), retries);

            }

            if (serviceManager.isServiceRunning(m_serviceName)) {
                LOG_ERROR("Failed to stop service '{}'!", ConvertWStringToString(m_serviceName));

                return false;
            }
        }

        LOG_INFO("Service '{}' successfully stopped.", ConvertWStringToString(m_serviceName));
        UpgradePathManager path;
        std::wstring m_backupPath = ConvertStringToWString(path.GetBackupPath()) + std::filesystem::path(m_targetPath).filename().wstring();

        if (fs::exists(m_targetPath)) {
            try {

                LOG_INFO("Checking paths before backup:");
                LOG_INFO("  Target path: '{}'", ConvertWStringToString(m_targetPath));
                LOG_INFO("  Backup path: '{}'", ConvertWStringToString(m_backupPath));

                if (m_backupPath == m_targetPath) {
                    LOG_WARN("Backup path is the same as the target path! Appending additional '.bak' suffix.");
                    m_backupPath = m_targetPath + L".bak";

                    LOG_INFO("New backup path: '{}'", ConvertWStringToString(m_backupPath));
                }

                LOG_INFO("Creating backup at '{}'", ConvertWStringToString(m_backupPath));

                fs::copy(m_targetPath, m_backupPath, fs::copy_options::overwrite_existing);

                LOG_INFO("Backup created at '{}'", ConvertWStringToString(m_backupPath));
            }
            catch (const std::exception& e) {
                LOG_ERROR("Failed to create backup: {}", e.what());
                return false;
            }
        }
        else {
            LOG_WARN("Target file '{}' does not exist, skipping backup.", ConvertWStringToString(m_targetPath));
        }


        try {
            LOG_INFO("Copying '{}' ? '{}'", ConvertWStringToString(m_newFilePath), ConvertWStringToString(m_targetPath));
            fs::copy(m_newFilePath, m_targetPath, fs::copy_options::overwrite_existing);
        }
        catch (const std::exception& e) {
            LOG_ERROR("Failed to copy new file: {}", e.what());
            rollback(wasServiceRunning);
            return false;
        }

        if (!fs::exists(m_targetPath)) {
            LOG_ERROR("File update failed: Target file '{}' does not exist after copying!", ConvertWStringToString(m_targetPath));
            rollback(wasServiceRunning);
            return false;
        }

        
        LOG_INFO("File updated successfully at '{}'", ConvertWStringToString(m_targetPath));
        LOG_INFO("Starting service '{}'", ConvertWStringToString(m_serviceName));

        for (int attempt = 1; attempt <= 5; ++attempt) {
            serviceManager.startService(m_serviceName);
            std::this_thread::sleep_for(std::chrono::seconds(3));

            if (serviceManager.isServiceRunning(m_serviceName)) {
                LOG_INFO("Service '{}' restarted successfully on attempt {}/5", ConvertWStringToString(m_serviceName), attempt);
                if (fs::exists(m_backupPath)) {
                    try {
                        fs::remove(m_backupPath);
                        LOG_INFO("Backup file '{}' deleted successfully.", ConvertWStringToString(m_backupPath));
                    }
                    catch (const std::exception& e) {
                        LOG_WARN("Failed to delete backup file '{}': {}", ConvertWStringToString(m_backupPath), e.what());
                    }
                }
                return true;
            }

            LOG_WARN("Service '{}' failed to start on attempt {}/5", ConvertWStringToString(m_serviceName), attempt);
        }

        LOG_ERROR("Service '{}' failed to start after 5 attempts!", ConvertWStringToString(m_serviceName));

        rollback(wasServiceRunning);
        return false;
    }

private:
    std::wstring m_serviceName;  ///< The name of the service to update.
    std::wstring m_newFilePath;  ///< The path to the new service executable.
    std::wstring m_targetPath;   ///< The target path where the service executable is located.

    /**
     * @brief Rolls back the service update if an error occurs.
     *
     * If the update fails, this function restores the previous executable from a backup
     * and attempts to restart the service if it was running before the update.
     *
     * @param wasServiceRunning Indicates whether the service was running before the update.
     */

    void rollback(bool wasServiceRunning) {
        WindowsServiceManager serviceManager;
        UpgradePathManager path;
        std::wstring m_backupPath = ConvertStringToWString(path.GetBackupPath()) + std::filesystem::path(m_targetPath).filename().wstring();


        if (wasServiceRunning) {
            LOG_INFO("Stopping service '{}' before rollback.", ConvertWStringToString(m_serviceName));
            serviceManager.stopService(m_serviceName);
        }

        if (fs::exists(m_backupPath)) {
            try {
                LOG_WARN("Rolling back to backup '{}'", ConvertWStringToString(m_backupPath));
                fs::copy(m_backupPath, m_targetPath, fs::copy_options::overwrite_existing);
                //spdlog::info("[ServiceRestartManager] Rollback successful: restored '{}'", ConvertWStringToString(m_targetPath));
                LOG_INFO("Rollback successful: restored '{}'", ConvertWStringToString(m_targetPath));

                if (wasServiceRunning) {
                    //spdlog::info("[ServiceRestartManager] Restarting service '{}' after rollback", ConvertWStringToString(m_serviceName));
                    LOG_INFO("Restarting service '{}' after rollback", ConvertWStringToString(m_serviceName));

                    serviceManager.startService(m_serviceName);

                    for (int attempt = 1; attempt <= 3; ++attempt) {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                        if (serviceManager.isServiceRunning(m_serviceName)) {
                            //spdlog::info("[ServiceRestartManager] Service '{}' restarted successfully after rollback.", ConvertWStringToString(m_serviceName));
                            LOG_INFO("Service '{}' restarted successfully after rollback.", ConvertWStringToString(m_serviceName));

                            return;
                        }
                        //spdlog::warn("[ServiceRestartManager] Service '{}' failed to start on rollback attempt {}/3", ConvertWStringToString(m_serviceName), attempt);
                        LOG_ERROR("Service '{}' failed to start on rollback attempt {}/3", ConvertWStringToString(m_serviceName), attempt);

                    }

                    //spdlog::error("[ServiceRestartManager] Service '{}' failed to restart after rollback!", ConvertWStringToString(m_serviceName));
                    LOG_ERROR("Service '{}' failed to restart after rollback!", ConvertWStringToString(m_serviceName));

                }
            }
            catch (const std::exception& e) {
                LOG_ERROR("Rollback failed: {}", e.what());
            }
        }
        else {
            LOG_ERROR("Rollback failed: No backup available.");
        }
    }
};

#endif // SERVICERESTARTMANAGER_H
