#ifndef FILEHASHER_H
#define FILEHASHER_H

#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <mutex>
#include <optional>
#include <ctime>
#include "Logger.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

class FileHasher {
public:
    explicit FileHasher(const std::string& jsonFilePath)
        : m_jsonFilePath(jsonFilePath) {
        CreateHashDirectory();
    }

    [[nodiscard]] std::optional<std::string> GetFileSHA256(const fs::path& filePath) const;
    void StoreFileHash(const std::string& filePath, const std::string& hash);
    [[nodiscard]] std::optional<std::string> GetStoredFileHash(const std::string& filePath) const;
    [[nodiscard]] bool HasFileChanged(const std::string& filePath, const std::string& currentHash) const;

    /**
     * @brief Static method that checks if the original file content has changed and updates the stored hash in JSON.
     * @param originalFilePath Path to the original file.
     * @param newFilePath Path to the new file.
     * @param jsonFilePath Path to the JSON file where hash information is stored.
     * @return True if the original file has changed and the JSON has been updated, false otherwise.
     */
    static bool CheckAndUpdateFileHash(const std::string& originalFilePath, const std::string& newFilePath, const std::string& jsonFilePath);

private:
    std::string m_jsonFilePath;
    mutable std::mutex m_mutex;

    void CreateHashDirectory();
    static std::string GetReadableTime(std::time_t rawTime);
    void ResetJsonFile() const;
};

#endif // FILEHASHER_H
