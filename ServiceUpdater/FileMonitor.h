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
 * @brief Proverava da li je fajl prisutan, upisuje hash u JSON i signalizira da je instalacija spremna.
 * @return true ako je instalacija validna, false ako nije.
 */
    [[nodiscard]] bool InitialInstall() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        try {
            //spdlog::info("Starting initial installation process for '{}'", m_configFilePath);
            LOG_INFO("Starting initial installation process for '{}'", m_configFilePath);

            // ? Proveri da li fajl postoji
            if (!fs::exists(m_configFilePath)) {
                //spdlog::error("Configuration file does not exist: {}", m_configFilePath);
                LOG_ERROR("Configuration file does not exist: {}", m_configFilePath);
                return false;
            }

            // ? Izra?unaj hash fajla
            auto currentHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (!currentHash) {
                //spdlog::error("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                LOG_ERROR("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                return false;
            }

            //spdlog::info("Computed hash: {}", *currentHash);
            LOG_INFO("Computed hash: {}", *currentHash);


            // ? Upisujemo hash u JSON (uvek, bez obzira da li postoji ili ne)
            //spdlog::info("Storing initial hash in JSON...");
            LOG_INFO("Storing initial hash in JSON...");
            m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
            //spdlog::info("Initial hash stored successfully.");
            LOG_INFO("Initial hash stored successfully.");

            return true; // ?? Instalacija je validna!
        }
        catch (const std::exception& e) {
            //spdlog::error("Error during initial installation process: {}", e.what());
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
            //spdlog::info("Checking if service restart is required...");
            LOG_INFO("Checking if service restart is required...");


            if (!fs::exists(m_configFilePath)) {
                //spdlog::warn("Configuration file does not exist: {}", m_configFilePath);
                LOG_WARN("Configuration file does not exist: {}", m_configFilePath);
                return false;
            }

            auto currentHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (!currentHash) {
                //spdlog::error("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                LOG_ERROR("Failed to compute SHA-256 hash for file: {}", m_configFilePath);
                return false;
            }
            //spdlog::info("Computed current hash: {}", *currentHash);
            LOG_INFO("Computed current hash: {}", *currentHash);

            auto storedHash = m_fileHasher.GetStoredFileHash(m_configFilePath);
            if (storedHash) {
                //spdlog::info("Stored hash from JSON: {}", *storedHash);
                LOG_INFO("Stored hash from JSON: {}", *storedHash);
            }
            else {
                //spdlog::warn("No stored hash found in JSON.");
                LOG_WARN("No stored hash found in JSON.");
            }

            //spdlog::info("m_firstTimeHashStored: {}", m_firstTimeHashStored ? "true" : "false");
            LOG_INFO("m_firstTimeHashStored: {}", m_firstTimeHashStored ? "true" : "false");

            if ((!storedHash || storedHash->empty()) && m_firstTimeHashStored) {
                //spdlog::info("First-time hash detected. Restart required.");
                LOG_INFO("First-time hash detected. Restart required.");
                m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
                m_restartRequired = true;
                m_firstTimeHashStored = false;  // Resetujemo flag nakon prvog restarta
                return true;
            }

            if (storedHash && (*storedHash != *currentHash)) {
                //spdlog::info("Configuration file has changed: {}", m_configFilePath);
                LOG_INFO("Configuration file has changed: {}", m_configFilePath);
                m_fileHasher.StoreFileHash(m_configFilePath, *currentHash);
                m_restartRequired = true;
                return true;
            }

            //spdlog::info("Configuration file is unchanged. No restart required.");
            LOG_INFO("Configuration file is unchanged. No restart required.");
            return false;
        }
        catch (const std::exception& e) {
            //spdlog::error("Error checking configuration file change: {}", e.what());
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
        //spdlog::info("Restart acknowledged. Service restart is no longer required.");
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
            //spdlog::warn("Failed to retrieve stored configuration hash: {}", e.what());
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
            //spdlog::warn("Configuration file does not exist initially: {}", m_configFilePath);
            LOG_WARN("Configuration file does not exist initially: {}", m_configFilePath);
            return;
        }

        //spdlog::info("Monitoring configuration file: {}", m_configFilePath);
        LOG_INFO("Monitoring configuration file: {}", m_configFilePath);

        auto storedHash = m_fileHasher.GetStoredFileHash(m_configFilePath);
        if (!storedHash) {
            auto initialHash = m_fileHasher.GetFileSHA256(m_configFilePath);
            if (initialHash) {
                //spdlog::info("No stored hash found, initializing with current hash.");
                LOG_INFO("No stored hash found, initializing with current hash.");
                m_fileHasher.StoreFileHash(m_configFilePath, *initialHash);
                m_firstTimeHashStored = true; 
                //spdlog::info("First-time initialization complete. Restart will be required.");
                LOG_INFO("First-time initialization complete. Restart will be required.");
            }
        }
    }
};

#endif // CONFIGFILEMONITOR_H
