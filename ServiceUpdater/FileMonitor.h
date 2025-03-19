#ifndef CONFIGFILEMONITOR_H
#define CONFIGFILEMONITOR_H

#include "FileHasher.h"
#include "Logger.h"
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <optional>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

/**
 * @class ConfigFileMonitor
 * @brief Manages configuration file state and determines if a service restart is necessary.
 */
class ConfigFileMonitor {
public:
    /**
     * @brief Constructs a ConfigFileMonitor object.
     * @param configFilePath Path to the configuration file to monitor.
     * @param jsonFilePath Path to the JSON file used for storing the file hash and metadata.
     */
    explicit ConfigFileMonitor(const fs::path& configFilePath, const fs::path& jsonFilePath)
        : m_fileHasher(jsonFilePath.string()),
        m_configFilePath(configFilePath.string()),
        m_restartRequired(false),
        m_firstTimeHashStored(true) {
        Initialize();
    }

    /**
     * @brief Performs the initial installation process by computing and storing the configuration file's hash.
     *
     * This function checks whether the configuration file exists, calculates its SHA-256 hash, and
     * stores it in a JSON file. If any step fails, it logs the error and returns `false`. If the
     * installation process is successful, it returns `true`.
     *
     * @return true if the initial installation process is successful, false otherwise.
     */
    [[nodiscard]] bool InitialInstall() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        try {
            LOG_INFO("Starting initial installation process for '{}'", m_configFilePath);

            if (!fs::exists(m_configFilePath)) {
                LOG_ERROR("Configuration file does not exist: {}", m_configFilePath);
                return false;
            }

            auto currentHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (!currentHash) {
                LOG_ERROR("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                return false;
            }

            LOG_INFO("Computed hash: {}", *currentHash);

            LOG_INFO("Storing initial hash in JSON...");
            m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
            LOG_INFO("Initial hash stored successfully.");

            return true; 
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error during initial installation process: {}", e.what());
            return false;
        }
    }


    /**
     * @brief Checks if the configuration file has been modified since the last recorded state.
     * @return True if the file has changed or if it's the first time storing the hash, false otherwise.
     */
    [[nodiscard]] bool ShouldRestartService() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        try {
            LOG_INFO("Checking if service restart is required...");


            if (!fs::exists(m_configFilePath)) {
                LOG_WARN("Configuration file does not exist: {}", m_configFilePath);
                return false;
            }

            auto currentHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (!currentHash) {
                LOG_ERROR("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                return false;
            }
            LOG_INFO("Computed current hash: {}", *currentHash);

            auto storedHash = m_fileHasher.GetStoredFileHash(m_configFilePath);
            if (storedHash) {
                LOG_INFO("Stored hash from JSON: {}", *storedHash);
            }
            else {
                LOG_WARN("No stored hash found in JSON.");
            }

            LOG_INFO("m_firstTimeHashStored: {}", m_firstTimeHashStored ? "true" : "false");

            if ((!storedHash || storedHash->empty()) && m_firstTimeHashStored) {
                LOG_INFO("First-time hash detected. Restart required.");
                m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
                m_restartRequired = true;
                m_firstTimeHashStored = false;  // Resetujemo flag nakon prvog restarta
                return true;
            }

            if (storedHash && (*storedHash != *currentHash)) {
                LOG_INFO("Configuration file has changed: {}", m_configFilePath);
                m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
                m_restartRequired = true;
                return true;
            }

            LOG_INFO("Configuration file is unchanged. No restart required.");
            return false;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error checking configuration file change: {}", e.what());
            return false;
        }
    }


    /**
     * @brief Updates internal state to indicate that restart has been handled.
     */
    void AcknowledgeRestart() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_restartRequired = false;
        LOG_INFO("Restart acknowledged. Service restart is no longer required.");
    }

    /**
     * @brief Retrieves the last known hash of the configuration file.
     * @return The stored hash as an optional string, or std::nullopt if unavailable.
     */
    [[nodiscard]] std::optional<std::string> GetStoredConfigHash() {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        try {
            return m_fileHasher.GetStoredFileHash(m_configFilePath);
        }
        catch (const std::exception& e) {
            LOG_WARN("Failed to retrieve stored configuration hash: {}", e.what());
            return std::nullopt;
        }
    }

    /**
     * @brief Checks if the service restart is currently required.
     * @return True if restart is needed, false otherwise.
     */
    [[nodiscard]] bool IsRestartRequired() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_restartRequired;
    }

private:
    mutable std::shared_mutex m_mutex;    ///< Mutex for thread-safe access.
    FileHasher m_fileHasher;               ///< Object responsible for computing and storing file hashes.
    const std::string m_configFilePath;    ///< Path to the configuration file being monitored.
    std::atomic<bool> m_restartRequired;   ///< Flag indicating if a restart is necessary.
    bool m_firstTimeHashStored;            ///< ? Flag to track if the hash was just initialized.

    /**
     * @brief Initializes the monitoring process by checking if the file exists and setting initial hash if needed.
     */
    void Initialize() {
        if (!fs::exists(m_configFilePath)) {
            LOG_WARN("Configuration file does not exist initially: {}", m_configFilePath);
            return;
        }

        LOG_INFO("Monitoring configuration file: {}", m_configFilePath);

        auto storedHash = m_fileHasher.GetStoredFileHash(m_configFilePath);
        if (!storedHash) {
            auto initialHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (initialHash) {
                LOG_INFO("No stored hash found, initializing with current hash.");
                m_fileHasher.StoreFileHash(m_configFilePath, *initialHash);
                m_firstTimeHashStored = true; 
                LOG_INFO("First-time initialization complete. Restart will be required.");
            }
        }
    }
};

#endif // CONFIGFILEMONITOR_H
