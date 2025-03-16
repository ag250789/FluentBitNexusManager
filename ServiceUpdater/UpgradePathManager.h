#ifndef UPGRADEPATHMANAGER_H
#define UPGRADEPATHMANAGER_H

#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace fs = std::filesystem;

class UpgradePathManager {
public:
    UpgradePathManager() {
        std::string upgradeRoot = GetUpgradeRootPath();
        m_upgradePath = upgradeRoot + "upgrade\\";
        m_configPath = m_upgradePath + "configs\\";
        m_zipPath = m_upgradePath + "zip\\";
        m_extractedPath = m_zipPath + "extracted\\";
        m_backupPath = m_zipPath + "backup\\";
        m_zipHashFilePath = m_zipPath + "zip_hashes.json";
        m_serviceHashFilePath = m_extractedPath + "service_hashes.json";
        m_blobName = "ncrv_dcs_streaming_service_upgrade_manager.zip";
        m_zipFilePath = m_zipPath + m_blobName;
        m_loggerConfig = m_configPath + "loggerConfig.json";
        m_proxyConfig = m_configPath + "proxyConfig.json";
        m_logDir = m_upgradePath + "logs\\";
        m_logFile = "dcsStreamingUpdate.log";
        m_mainConfig = m_configPath + "serviceMainConfig.json";
        m_controllerConfig = m_configPath + "DCSAgentDataStreamConfig.json";
        m_uninstallDir = GetServiceInstallPath();
    }

    static void EnsureUpgradeDirectoriesExist() {
        UpgradePathManager pathManager;

        // ? Kreiramo sve potrebne direktorijume ako ne postoje
        std::vector<std::string> directories = {
            pathManager.GetUpgradeDirectory(),
            pathManager.GetZipDirectory(),
            pathManager.GetExtractedPath(),
            pathManager.GetConfigsDirectory(),
            pathManager.GetLogDirectory(),
            pathManager.GetBackupPath()
        };

        for (const auto& dir : directories) {
            if (!fs::exists(dir)) {
                try {
                    fs::create_directories(dir);
                    spdlog::info("Created missing directory: {}", dir);
                }
                catch (const std::exception& e) {
                    spdlog::error("Failed to create directory '{}': {}", dir, e.what());
                }
            }
            else {
                spdlog::info("Directory already exists: {}", dir);
            }
        }
    }

    static bool copy_file_robust(const std::string& source, const std::string& destination) {
        try {
            // Logovanje po?etka operacije
            spdlog::info("Starting file copy: {} -> {}", source, destination);


            // Provera da li fajl postoji
            if (!fs::exists(source)) {
                spdlog::error("Source file does not exist: {}", source);

                return false;
            }

            // Dobijanje putanje direktorijuma odredišnog fajla
            fs::path destPath = destination;
            fs::path destDir = destPath.parent_path();

            // Ako direktorijum ne postoji, kreiraj ga
            if (!fs::exists(destDir)) {
                spdlog::info("Creating directory: {}", destDir.string());

                fs::create_directories(destDir);
            }

            // Kopiranje fajla
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
            spdlog::info("File copied successfully from {} to {}", source, destination);


            return true;
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Filesystem error: {}", e.what());

        }
        catch (const std::exception& e) {
            spdlog::error("General error: {}", e.what());

        }
        catch (...) {
            spdlog::error("Unknown error occurred during file copy.");

        }
        return false;
    }

    std::string GetBackupPath() const {
        return m_backupPath;
    }

    std::string GetMainConfig() const {
        return m_mainConfig;
    }

    std::string GetControllerConfig() const {
        return m_controllerConfig;
    }

    std::string GetLogDirectory() const {
        return m_logDir;
    }

    std::string GetLogPath() const {
        return m_logFile;
    }

    // ? Putanja do upgrade foldera
    std::string GetUpgradeDirectory() const {
        return m_upgradePath;
    }

    // ? Putanja do ZIP fajlova
    std::string GetZipDirectory() const {
        return m_zipPath;
    }

    std::string GetConfigsDirectory() const {
        return m_configPath;
    }

    // ? Putanja gde su fajlovi ekstrahovani
    std::string GetExtractedPath() const {
        return m_extractedPath;
    }

    // ? Putanja do ZIP fajla sa nadogradnjom
    std::string GetZipFilePath() const {
        return m_zipFilePath;
    }

    std::string GetLoggerFilePath() const {
        return m_loggerConfig;
    }

    std::string GetProxyFilePath() const {
        return m_proxyConfig;
    }

    // ? Putanja do JSON fajla sa hash vrednostima ZIP fajlova
    std::string GetZipHashFilePath() const {
        return m_zipHashFilePath;
    }

    std::string GetRootDir() const {
        return GetUpgradeRootPath();
    }

    // ? Putanja do JSON fajla sa hash vrednostima servisa
    std::string GetServiceHashFilePath() const {
        return m_serviceHashFilePath;
    }

    // ? Ime ZIP fajla koji se koristi za update
    std::string GetBlobName() const {
        return m_blobName;
    }

    std::string GetCleanDir() const {
        return m_uninstallDir;
    }

    // ? Putanja do prvog servisa (FluentBitManager)
    std::wstring GetService1TargetPath() const {
        return ConvertStringToWString(GetServiceInstallPath() + "FluentBitManager.exe");
    }

    // ? Putanja do drugog servisa (WatchdogFluentBit)
    std::wstring GetService2TargetPath() const {
        return ConvertStringToWString(GetServiceInstallPath() + "watchdog\\WatchdogFluentBit.exe");
    }

    std::wstring GetService3TargetPath() const {
        return ConvertStringToWString(GetServiceInstallPath() + "data\\bin\\fluent-bit.exe");
    }

    std::string GetServiceConfigPath() const {
        return (GetServiceInstallPath() + "service_configuration\\DCSAgentDataStreamConfig.json");
    }

    // ? Ime prvog servisa
    std::wstring GetService1Name() const {
        return L"DCSStreamingAgentController";
    }

    // ? Ime drugog servisa
    std::wstring GetService2Name() const {
        return L"DCSStreamingAgentWatchdog";
    }

    std::wstring GetService3Name() const {
        return L"DCSStreamingAgent";
    }

    static bool SecureDeleteFile(const std::string& filePath) {
        try {
            if (!std::filesystem::exists(filePath)) {
                spdlog::warn("File does not exist: {}", filePath);
                return false;
            }

            std::size_t fileSize = std::filesystem::file_size(filePath);
            std::ofstream file(filePath, std::ios::binary | std::ios::out);

            if (!file) {
                spdlog::error("Failed to open file for secure deletion: {}", filePath);
                return false;
            }

            // Višestruko prepisivanje (3 puta) sa nasumi?nim podacima
            for (int pass = 0; pass < 3; ++pass) {
                file.seekp(0);
                for (std::size_t i = 0; i < fileSize; ++i) {
                    file.put(static_cast<char>(rand() % 256));
                }
                file.flush();
            }
            file.close();

            // Uklanjanje fajla nakon prepisivanja
            std::filesystem::remove(filePath);
            spdlog::info("Securely deleted file: {}", filePath);
            return true;

        }
        catch (const std::exception& e) {
            spdlog::error("Exception while securely deleting file {}: {}", filePath, e.what());
            return false;
        }
    }

    
   
private:
    std::string m_upgradePath;
    std::string m_zipPath;
    std::string m_extractedPath;
    std::string m_zipFilePath;
    std::string m_zipHashFilePath;
    std::string m_serviceHashFilePath;
    std::string m_blobName;
    std::string m_configPath;
    std::string m_loggerConfig;
    std::string m_proxyConfig;
    std::string m_logDir;
    std::string m_logFile;
    std::string m_mainConfig;
    std::string m_uninstallDir;
    std::string m_controllerConfig;
    std::string m_backupPath;





    /**
     * @brief Proverava da li je sistem 64-bitni ili 32-bitni.
     * @return true ako je 64-bitni, false ako je 32-bitni.
     */
    bool Is64BitSystem() const {
#if defined(_WIN64)
        return true;  // 64-bitni proces na 64-bitnom sistemu
#elif defined(_WIN32)
        BOOL isWow64 = FALSE;
        return IsWow64Process(GetCurrentProcess(), &isWow64) && isWow64; // 32-bitni proces na 64-bitnom sistemu
#else
        return false; // Stariji procesori
#endif
    }

    /**
     * @brief Vra?a osnovni direktorijum gde se nalazi `Upgrade` folder.
     * Automatski bira `Program Files` ili `Program Files (x86)`.
     */
    std::string GetUpgradeRootPath() const {
        return Is64BitSystem()
            ? "C:\\Program Files (x86)\\NCR\\CSM2.0\\"
            : "C:\\Program Files\\NCR\\CSM2.0\\";
    }

    /**
     * @brief Vra?a osnovni direktorijum gde su instalirani servisi.
     * Automatski bira `Program Files` ili `Program Files (x86)`.
     */
    std::string GetServiceInstallPath() const {
        return Is64BitSystem()
            ? "C:\\Program Files (x86)\\NCR\\CSM2.0\\DCS Streaming\\"
            : "C:\\Program Files\\NCR\\CSM2.0\\DCS Streaming\\";
    }

    /**
     * @brief Konvertuje `std::string` u `std::wstring`.
     */
    std::wstring ConvertStringToWString(const std::string& str) const {
        if (str.empty()) return L"";

        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (size_needed <= 0) {
            return L""; // Greška u konverziji
        }

        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);

        return wstr;
    }

    

    std::string ConvertWStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string str(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, nullptr, nullptr);
        return str;
    }
};

#endif // UPGRADEPATHMANAGER_H
