#include "FileHasher.h"
#include <string>
#include <regex>

std::string NormalizePath(const std::string& path) {
    std::regex doubleBackslash(R"(\\+)");  // Regularni izraz za duple i više backslash karaktere
    return std::regex_replace(path, doubleBackslash, R"(\)"); // Zamenjuje ih jednim backslashom
}


std::wstring NormalizePath(const std::wstring& path) {
    std::wregex doubleBackslash(L"\\\\+");  // Regularni izraz za duple i više backslash karaktere
    return std::regex_replace(path, doubleBackslash, L"\\"); // Zamenjuje ih jednim backslashom
}

std::optional<std::string> FileHasher::GetFileSHA256(const fs::path& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        //spdlog::error("Failed to open file for SHA-256 calculation: {}", filePath.string());
        LOG_ERROR("Failed to open file for SHA-256 calculation: {}", filePath.string());

        return std::nullopt;
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        //spdlog::error("Failed to create EVP_MD_CTX for hashing.");
        LOG_ERROR("Failed to create EVP_MD_CTX for hashing.");

        return std::nullopt;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(mdctx);
        //spdlog::error("Failed to initialize SHA-256 context.");
        LOG_ERROR("Failed to initialize SHA-256 context.");

        return std::nullopt;
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(mdctx);
            //spdlog::error("Failed to update SHA-256 digest.");
            LOG_ERROR("Failed to update SHA-256 digest.");

            return std::nullopt;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int lengthOfHash = 0;
    if (EVP_DigestFinal_ex(mdctx, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(mdctx);
        //spdlog::error("Failed to finalize SHA-256 digest.");
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

void FileHasher::StoreFileHash(const std::string& fileDirtyPath, const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string filePath = NormalizePath(fileDirtyPath);
    json j;

    // ? Proveri da li JSON fajl postoji, ako ne kreiraj ga
    if (fs::exists(m_jsonFilePath)) {
        std::ifstream configFile(m_jsonFilePath);
        if (configFile.is_open()) {
            try {
                configFile >> j;
                if (!j.is_object()) {
                    //spdlog::warn("JSON file is corrupted, resetting to empty JSON: {}", m_jsonFilePath);
                    LOG_WARN("JSON file is corrupted, resetting to empty JSON: {}", m_jsonFilePath);

                    j = json::object();
                }
            }
            catch (const std::exception& e) {
                //spdlog::warn("Invalid JSON format in {}: {}", m_jsonFilePath, e.what());
                LOG_WARN("Invalid JSON format in {}: {}", m_jsonFilePath, e.what());

                j = json::object();
            }
        }
        configFile.close();
    }

    std::time_t currentTime = std::time(nullptr);

    // ? Dodaj ili ažuriraj zapis za konkretan fajl
    j[filePath] = {
        {"file_hash", hash},
        {"timestamp", currentTime},
        {"readable_timestamp", GetReadableTime(currentTime)}
    };

    // ? Upisujemo ažurirani JSON nazad u fajl
    std::ofstream outFile(m_jsonFilePath);
    if (!outFile.is_open()) {
        //spdlog::error("Failed to open JSON file for writing: {}", m_jsonFilePath);
        LOG_ERROR("Failed to open JSON file for writing: {}", m_jsonFilePath);

        return;
    }

    outFile << j.dump(4);
    //spdlog::info("Updated file hash for '{}' in JSON file '{}'", filePath, m_jsonFilePath);
    LOG_INFO("Updated file hash for '{}' in JSON file '{}'", filePath, m_jsonFilePath);

}


std::optional<std::string> FileHasher::GetStoredFileHash(const std::string& fileDirtyPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string filePath = NormalizePath(fileDirtyPath);

    if (!fs::exists(m_jsonFilePath)) {
        //spdlog::warn("JSON file does not exist, creating an empty JSON file: {}", m_jsonFilePath);
        LOG_WARN("JSON file does not exist, creating an empty JSON file: {}", m_jsonFilePath);

        ResetJsonFile();
        return std::nullopt;
    }

    std::ifstream configFile(m_jsonFilePath);
    if (!configFile.is_open()) {
        //spdlog::error("Unable to open JSON file: {}", m_jsonFilePath);
        LOG_ERROR("Unable to open JSON file: {}", m_jsonFilePath);

        return std::nullopt;
    }

    json j;
    try {
        configFile >> j;
        configFile.close();

        //spdlog::info("JSON file content: {}", j.dump(4));  // Debug ispis celog JSON-a
        LOG_INFO("JSON file content: {}", j.dump(4));  // Debug ispis celog JSON-a

        // ? Provera da li je struktura validna
        if (!j.is_object()) {
            //spdlog::warn("JSON file is corrupted, resetting to an empty JSON: {}", m_jsonFilePath);
            LOG_WARN("JSON file is corrupted, resetting to an empty JSON: {}", m_jsonFilePath);

            ResetJsonFile();
            return std::nullopt;
        }

        // ? Proveravamo da li postoji zapis za dati fajl
        if (j.contains(filePath) && j[filePath].contains("file_hash")) {
            std::string storedHash = j[filePath]["file_hash"].get<std::string>();
            //spdlog::info("Retrieved stored hash: {}", storedHash);
            LOG_INFO("Retrieved stored hash: {}", storedHash);

            return storedHash;
        }

        //spdlog::warn("No hash record found for file: {}", filePath);
        LOG_WARN("No hash record found for file: {}", filePath);

        return std::nullopt;
    }
    catch (const std::exception& e) {
        //spdlog::error("Failed to read stored hash from JSON: {}", e.what());
        LOG_ERROR("Failed to read stored hash from JSON: {}", e.what());

        ResetJsonFile();
        return std::nullopt;
    }
}


/**
 * @brief Resetuje JSON fajl na prazan objekat `{}` kako bi spre?io korupciju podataka.
 */
void FileHasher::ResetJsonFile() const {
    try {
        std::ofstream resetFile(m_jsonFilePath);
        if (resetFile.is_open()) {
            resetFile << "{}";
            resetFile.close();
            //spdlog::info("Successfully reset JSON file: {}", m_jsonFilePath);
            LOG_INFO("Successfully reset JSON file: {}", m_jsonFilePath);

        }
        else {
            //spdlog::error("Failed to reset JSON file: {}", m_jsonFilePath);
            LOG_ERROR("Failed to reset JSON file: {}", m_jsonFilePath);

        }
    }
    catch (const std::exception& e) {
        //spdlog::error("Exception while resetting JSON file: {}", e.what());
        LOG_ERROR("Exception while resetting JSON file: {}", e.what());

    }
}


bool FileHasher::HasFileChanged(const std::string& filePath, const std::string& currentHash) const {
    auto storedHash = GetStoredFileHash(filePath);
    return !storedHash || *storedHash != currentHash;
}

bool FileHasher::CheckAndUpdateFileHash(const std::string& originalFilePath, const std::string& newFilePath, const std::string& jsonFilePath) {
    FileHasher hasher(jsonFilePath);

    if (!fs::exists(originalFilePath)) {
        //spdlog::error("Original file does not exist: {}", originalFilePath);
        LOG_ERROR("Original file does not exist: {}", originalFilePath);

        return false;
    }

    auto originalHash = hasher.GetFileSHA256(originalFilePath);
    if (!originalHash) {
        //spdlog::error("Failed to compute hash for original file: {}", originalFilePath);
        LOG_ERROR("Failed to compute hash for original file: {}", originalFilePath);

        return false;
    }

    if (!fs::exists(newFilePath)) {
        //spdlog::error("New file does not exist: {}", newFilePath);
        LOG_ERROR("New file does not exist: {}", newFilePath);
        return false;
    }

    auto newHash = hasher.GetFileSHA256(newFilePath);
    if (!newHash) {
        //spdlog::error("Failed to compute hash for new file: {}", newFilePath);
        LOG_ERROR("Failed to compute hash for new file: {}", newFilePath);

        return false;
    }

    //spdlog::info("Original file hash: {}", *originalHash);
    //spdlog::info("New file hash: {}", *newHash);
    LOG_INFO("Original file hash: {}", *originalHash);
    LOG_INFO("New file hash: {}", *newHash);

    // ? Ako su originalni i novi hash isti, ne radimo ništa
    if (*originalHash == *newHash) {
        //spdlog::info("File is unchanged. No update needed.");
        LOG_INFO("File is unchanged. No update needed.");


        // Ako JSON ne postoji, ipak ga kreiramo da bi postojao zapis, ali ne restartujemo
        if (!fs::exists(jsonFilePath)) {
            //spdlog::warn("JSON file '{}' does not exist. Creating new one...", jsonFilePath);
            LOG_WARN("JSON file '{}' does not exist. Creating new one...", jsonFilePath);

            hasher.StoreFileHash(originalFilePath, *newHash);
            //spdlog::info("JSON record created successfully (no update needed).");
            LOG_INFO("JSON record created successfully (no update needed).");

        }

        return false;  // **Nema potrebe za restartom jer su fajlovi isti**
    }

    if (!fs::exists(jsonFilePath)) {
        //spdlog::warn("JSON file does not exist, creating new one: {}", jsonFilePath);
        LOG_WARN("JSON file does not exist, creating new one: {}", jsonFilePath);

        hasher.StoreFileHash(originalFilePath, *newHash);
        //spdlog::info("JSON record created successfully.");
        LOG_INFO("JSON record created successfully.");

        return true;
    }

    auto storedHash = hasher.GetStoredFileHash(originalFilePath);
    if (!storedHash) {
        //spdlog::warn("JSON file exists but does not contain valid data. Recreating...");
        LOG_WARN("JSON file exists but does not contain valid data. Recreating...");

        hasher.StoreFileHash(originalFilePath, *newHash);
        return true;
    }
    else {
        LOG_INFO("Stored file hash: {}", *storedHash);
    }

    if ((*storedHash == *newHash) && (*originalHash == *storedHash)) {
        //spdlog::info("Original file has not changed. No update needed.");
        LOG_INFO("Original file has not changed. No update needed.");

        return false;
    }

    if ((*storedHash == *newHash) && (*originalHash != *storedHash)) {
        //spdlog::info("New file is already recorded in JSON, but original file has changed.");
        LOG_INFO("New file is already recorded in JSON, but original file has changed.");
        return true;
    }


    //spdlog::info("Original file has changed. Updating JSON record...");
    LOG_INFO("Original file has changed. Updating JSON record...");
    hasher.StoreFileHash(originalFilePath, *newHash);
    //spdlog::info("JSON record updated successfully.");
    LOG_INFO("JSON record updated successfully.");

    return true;
}

void FileHasher::CreateHashDirectory() {
    fs::path hashDir = fs::path(m_jsonFilePath).parent_path();
    if (!fs::exists(hashDir)) {
        fs::create_directories(hashDir);
    }
}

std::string FileHasher::GetReadableTime(std::time_t rawTime) {
    std::tm* timeInfo = std::localtime(&rawTime);
    std::ostringstream oss;
    oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
