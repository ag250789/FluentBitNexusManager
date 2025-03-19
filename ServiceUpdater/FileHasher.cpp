#include "FileHasher.h"
#include <string>
#include <regex>

/**
 * @brief Normalizes a file path by replacing multiple backslashes with a single backslash.
 *
 * This function ensures that Windows-style paths are correctly formatted by removing
 * redundant backslashes, making the path more consistent and avoiding potential issues
 * when working with file operations.
 *
 * @param path The file path to normalize.
 * @return A normalized file path with redundant backslashes removed.
 */
std::string NormalizePath(const std::string& path) {
    std::regex doubleBackslash(R"(\\+)");  
    return std::regex_replace(path, doubleBackslash, R"(\)"); 
}

/**
 * @brief Normalizes a wide-character file path by replacing multiple backslashes with a single backslash.
 *
 * This function performs the same normalization as the `std::string` version but works with
 * `std::wstring` to support Unicode paths.
 *
 * @param path The wide-character file path to normalize.
 * @return A normalized wide-character file path with redundant backslashes removed.
 */
std::wstring NormalizePath(const std::wstring& path) {
    std::wregex doubleBackslash(L"\\\\+");  
    return std::regex_replace(path, doubleBackslash, L"\\"); 
}

/**
 * @brief Computes the SHA-256 hash of a given file.
 *
 * This function reads the content of the specified file in binary mode and calculates its
 * SHA-256 hash using OpenSSL's EVP functions. If the file cannot be opened or hashing fails,
 * it logs an error and returns `std::nullopt`. Otherwise, it returns the computed hash as a
 * hexadecimal string.
 *
 * @param filePath The path to the file whose SHA-256 hash is to be calculated.
 * @return An optional string containing the SHA-256 hash of the file, or `std::nullopt` on failure.
 */
std::optional<std::string> FileHasher::GetFileSHA256(const fs::path& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for SHA-256 calculation: {}", filePath.string());

        return std::nullopt;
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        LOG_ERROR("Failed to create EVP_MD_CTX for hashing.");

        return std::nullopt;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(mdctx);
        LOG_ERROR("Failed to initialize SHA-256 context.");

        return std::nullopt;
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(mdctx);
            LOG_ERROR("Failed to update SHA-256 digest.");

            return std::nullopt;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int lengthOfHash = 0;
    if (EVP_DigestFinal_ex(mdctx, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(mdctx);
        LOG_ERROR("Failed to finalize SHA-256 digest.");

        return std::nullopt;
    }

    EVP_MD_CTX_free(mdctx);
    file.close();

    std::ostringstream oss;
    for (unsigned int i = 0; i < lengthOfHash; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return oss.str();
}
/**
 * @brief Stores or updates the SHA-256 hash of a file in a JSON file.
 *
 * This function normalizes the given file path, checks if the JSON file exists, and reads its
 * contents. If the JSON file is corrupted or invalid, it resets it to an empty JSON object.
 * The function then updates the JSON with the new hash, timestamp, and a human-readable timestamp
 * before saving the updated JSON back to the file. The operation is thread-safe using a mutex lock.
 *
 * @param fileDirtyPath The original file path (which will be normalized).
 * @param hash The SHA-256 hash of the file.
 */
void FileHasher::StoreFileHash(const std::string& fileDirtyPath, const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string filePath = NormalizePath(fileDirtyPath);
    json j;

    if (fs::exists(m_jsonFilePath)) {
        std::ifstream configFile(m_jsonFilePath);
        if (configFile.is_open()) {
            try {
                configFile >> j;
                if (!j.is_object()) {
                    LOG_WARN("JSON file is corrupted, resetting to empty JSON: {}", m_jsonFilePath);

                    j = json::object();
                }
            }
            catch (const std::exception& e) {
                LOG_WARN("Invalid JSON format in {}: {}", m_jsonFilePath, e.what());

                j = json::object();
            }
        }
        configFile.close();
    }

    std::time_t currentTime = std::time(nullptr);

    j[filePath] = {
        {"file_hash", hash},
        {"timestamp", currentTime},
        {"readable_timestamp", GetReadableTime(currentTime)}
    };

    std::ofstream outFile(m_jsonFilePath);
    if (!outFile.is_open()) {
        LOG_ERROR("Failed to open JSON file for writing: {}", m_jsonFilePath);

        return;
    }

    outFile << j.dump(4);
    LOG_INFO("Updated file hash for '{}' in JSON file '{}'", filePath, m_jsonFilePath);

}

/**
 * @brief Retrieves the stored SHA-256 hash of a file from a JSON file.
 *
 * This function normalizes the file path and checks if the JSON file exists. If the JSON file
 * is missing or corrupted, it resets it to an empty JSON object. It then attempts to extract
 * the stored hash for the given file. If the file has an associated hash, it is returned.
 * Otherwise, it logs a warning and returns `std::nullopt`.
 *
 * @param fileDirtyPath The original file path (which will be normalized).
 * @return An optional string containing the stored SHA-256 hash, or `std::nullopt` if not found.
 */
std::optional<std::string> FileHasher::GetStoredFileHash(const std::string& fileDirtyPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string filePath = NormalizePath(fileDirtyPath);

    if (!fs::exists(m_jsonFilePath)) {
        LOG_WARN("JSON file does not exist, creating an empty JSON file: {}", m_jsonFilePath);

        ResetJsonFile();
        return std::nullopt;
    }

    std::ifstream configFile(m_jsonFilePath);
    if (!configFile.is_open()) {
        LOG_ERROR("Unable to open JSON file: {}", m_jsonFilePath);

        return std::nullopt;
    }

    json j;
    try {
        configFile >> j;
        configFile.close();

        LOG_INFO("JSON file content: {}", j.dump(4)); 

        if (!j.is_object()) {
            LOG_WARN("JSON file is corrupted, resetting to an empty JSON: {}", m_jsonFilePath);

            ResetJsonFile();
            return std::nullopt;
        }

        if (j.contains(filePath) && j[filePath].contains("file_hash")) {
            std::string storedHash = j[filePath]["file_hash"].get<std::string>();
            LOG_INFO("Retrieved stored hash: {}", storedHash);

            return storedHash;
        }

        LOG_WARN("No hash record found for file: {}", filePath);

        return std::nullopt;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to read stored hash from JSON: {}", e.what());

        ResetJsonFile();
        return std::nullopt;
    }
}


/**
 * @brief Resets the JSON file to an empty object `{}` to prevent data corruption.
 *
 * If the JSON file is found to be missing or corrupted, this function overwrites it with an
 * empty JSON object to maintain integrity. It logs the reset operation and handles any exceptions
 * that might occur.
 */
void FileHasher::ResetJsonFile() const {
    try {
        std::ofstream resetFile(m_jsonFilePath);
        if (resetFile.is_open()) {
            resetFile << "{}";
            resetFile.close();
            LOG_INFO("Successfully reset JSON file: {}", m_jsonFilePath);

        }
        else {
            LOG_ERROR("Failed to reset JSON file: {}", m_jsonFilePath);

        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception while resetting JSON file: {}", e.what());

    }
}

/**
 * @brief Checks if a file has changed by comparing its current SHA-256 hash with the stored hash.
 *
 * This function retrieves the stored hash of the file and compares it with the given current hash.
 * If no stored hash is found or the hashes do not match, the function returns `true` indicating
 * the file has changed.
 *
 * @param filePath The path of the file to check.
 * @param currentHash The computed SHA-256 hash of the file.
 * @return true if the file has changed, false otherwise.
 */
bool FileHasher::HasFileChanged(const std::string& filePath, const std::string& currentHash) const {
    auto storedHash = GetStoredFileHash(filePath);
    return !storedHash || *storedHash != currentHash;
}
/**
 * @brief Checks if a file has changed and updates its hash record in JSON if necessary.
 *
 * This function compares the SHA-256 hashes of an original file and a new file. It updates
 * the stored hash in the JSON file if changes are detected. If the file remains unchanged,
 * it avoids unnecessary updates. It also ensures the JSON file exists and contains valid data.
 *
 * @param originalFilePath The path to the original file.
 * @param newFilePath The path to the new file for comparison.
 * @param jsonFilePath The path to the JSON file storing file hashes.
 * @return true if the file has changed and the JSON record was updated, false otherwise.
 */
bool FileHasher::CheckAndUpdateFileHash(const std::string& originalFilePath, const std::string& newFilePath, const std::string& jsonFilePath) {
    FileHasher hasher(jsonFilePath);

    if (!fs::exists(originalFilePath)) {
        LOG_ERROR("Original file does not exist: {}", originalFilePath);

        return false;
    }

    auto originalHash = hasher.GetFileSHA256(originalFilePath);
    if (!originalHash) {
        LOG_ERROR("Failed to compute hash for original file: {}", originalFilePath);

        return false;
    }

    if (!fs::exists(newFilePath)) {
        LOG_ERROR("New file does not exist: {}", newFilePath);
        return false;
    }

    auto newHash = hasher.GetFileSHA256(newFilePath);
    if (!newHash) {
        LOG_ERROR("Failed to compute hash for new file: {}", newFilePath);

        return false;
    }

    LOG_INFO("Original file hash: {}", *originalHash);
    LOG_INFO("New file hash: {}", *newHash);

    if (*originalHash == *newHash) {
        LOG_INFO("File is unchanged. No update needed.");


        if (!fs::exists(jsonFilePath)) {
            LOG_WARN("JSON file '{}' does not exist. Creating new one...", jsonFilePath);

            hasher.StoreFileHash(originalFilePath, *newHash);
            LOG_INFO("JSON record created successfully (no update needed).");

        }

        return false;  
    }

    if (!fs::exists(jsonFilePath)) {
        LOG_WARN("JSON file does not exist, creating new one: {}", jsonFilePath);

        hasher.StoreFileHash(originalFilePath, *newHash);
        LOG_INFO("JSON record created successfully.");

        return true;
    }

    auto storedHash = hasher.GetStoredFileHash(originalFilePath);
    if (!storedHash) {
        LOG_WARN("JSON file exists but does not contain valid data. Recreating...");

        hasher.StoreFileHash(originalFilePath, *newHash);
        return true;
    }
    else {
        LOG_INFO("Stored file hash: {}", *storedHash);
    }

    if ((*storedHash == *newHash) && (*originalHash == *storedHash)) {
        LOG_INFO("Original file has not changed. No update needed.");

        return false;
    }

    if ((*storedHash == *newHash) && (*originalHash != *storedHash)) {
        LOG_INFO("New file is already recorded in JSON, but original file has changed.");
        return true;
    }


    LOG_INFO("Original file has changed. Updating JSON record...");
    hasher.StoreFileHash(originalFilePath, *newHash);
    LOG_INFO("JSON record updated successfully.");

    return true;
}

/**
 * @brief Creates the directory for storing the hash JSON file if it does not exist.
 *
 * This function ensures that the directory where the JSON file is stored exists by creating
 * it if necessary. This prevents issues with missing directories when attempting to save hashes.
 */
void FileHasher::CreateHashDirectory() {
    fs::path hashDir = fs::path(m_jsonFilePath).parent_path();
    if (!fs::exists(hashDir)) {
        fs::create_directories(hashDir);
    }
}

/**
 * @brief Converts a raw time value to a human-readable timestamp format.
 *
 * This function formats a given `std::time_t` value into a string representation
 * using the format "YYYY-MM-DD HH:MM:SS".
 *
 * @param rawTime The raw time value to format.
 * @return A formatted string representation of the timestamp.
 */
std::string FileHasher::GetReadableTime(std::time_t rawTime) {
    std::tm* timeInfo = std::localtime(&rawTime);
    std::ostringstream oss;
    oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
