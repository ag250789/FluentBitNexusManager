#include "ZipManager.h"
#include <filesystem>
#include <spdlog/spdlog.h>


namespace fs = std::filesystem;

ZipManager::ZipManager() {}

ZipManager::~ZipManager() {}

bool ZipManager::AddFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipArchive::Ptr archive = ZipFile::Open(zipFilename);

        std::string finalEntryName = entryName.empty() ? fs::path(fileToAdd).filename().string() : entryName;

        ZipArchiveEntry::Ptr entry = archive->CreateEntry(finalEntryName);
        if (!entry) {
            LOG_ERROR("Failed to create entry in archive: {}", finalEntryName);

            return false;
        }

        std::ifstream fileStream(fileToAdd, std::ios::binary);
        if (!fileStream.is_open()) {
            LOG_ERROR("Failed to open file: {}", fileToAdd);

            return false;
        }

        entry->SetCompressionStream(fileStream, DeflateMethod::Create(), ZipArchiveEntry::CompressionMode::Immediate);

        ZipFile::SaveAndClose(archive, zipFilename);

        LOG_INFO("File '{}' added to '{}' as '{}'", fileToAdd, zipFilename, finalEntryName);

        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to add file to archive: {}", e.what());

        return false;
    }
}



bool ZipManager::AddEncryptedFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName, const std::string& password) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipFile::AddEncryptedFile(zipFilename, fileToAdd, entryName, password);
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to add encrypted file to archive: {}", e.what());

        return false;
    }
}

bool ZipManager::ExtractFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipArchive::Ptr archive = ZipFile::Open(zipFilename);
        if (!archive) {
            LOG_ERROR("Failed to open archive: {}", zipFilename);

            return false;
        }

        ZipArchiveEntry::Ptr entry = archive->GetEntry(entryName);
        if (!entry) {
            LOG_ERROR("Entry not found in archive: {}", entryName);

            return false;
        }

        if (entry->IsDirectory()) {
            LOG_ERROR("Cannot extract directory: {}", entryName);

            return false;
        }

        std::filesystem::create_directories(std::filesystem::path(outputFilename).parent_path());

        ZipFile::ExtractFile(zipFilename, entryName, outputFilename);

        LOG_INFO("Extracted '{}' to '{}'", entryName, outputFilename);

        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to extract file from archive: {}", e.what());

        return false;
    }
}


bool ZipManager::ExtractEncryptedFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename, const std::string& password) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipFile::ExtractEncryptedFile(zipFilename, entryName, outputFilename, password);
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to extract encrypted file from archive: {}", e.what());

        return false;
    }
}

bool ZipManager::RemoveEntryFromArchive(const std::string& zipFilename, const std::string& entryName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipFile::RemoveEntry(zipFilename, entryName);
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to remove entry from archive: {}", e.what());

        return false;
    }
}

std::vector<std::string> ZipManager::ListArchiveContents(const std::string& zipFilename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> contents;
    try {
        ZipArchive::Ptr archive = ZipFile::Open(zipFilename);
        size_t entries = archive->GetEntriesCount();

        for (size_t i = 0; i < entries; ++i) {
            auto entry = archive->GetEntry(static_cast<int>(i));
            contents.push_back(entry->GetFullName());
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to list archive contents: {}", e.what());

    }
    return contents;
}

bool ZipManager::ExtractArchiveToFolder(const std::string& zipFilename, const std::string& outputFolder) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipArchive::Ptr archive = ZipFile::Open(zipFilename);

        for (size_t i = 0; i < archive->GetEntriesCount(); ++i) {
            auto entry = archive->GetEntry(static_cast<int>(i));
            std::string outputPath = outputFolder + "/" + entry->GetFullName();

            if (entry->IsDirectory()) {
                fs::create_directories(outputPath);
            }
            else {
                fs::create_directories(fs::path(outputPath).parent_path());
                ZipFile::ExtractFile(zipFilename, entry->GetFullName(), outputPath);
            }
        }
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to extract archive: {}", e.what());

        return false;
    }
}

void ZipManager::AddFolderToArchive(const std::string& folderPath, ZipArchive::Ptr archive, const std::string& basePath) {
    for (const auto& entry : fs::recursive_directory_iterator(folderPath)) {
        std::string relativePath = fs::relative(entry.path(), basePath).string();

        if (entry.is_directory()) {
            archive->CreateEntry(relativePath + "/");
        }
        else {
            ZipArchiveEntry::Ptr zipEntry = archive->CreateEntry(relativePath);
            std::ifstream file(entry.path(), std::ios::binary);
            zipEntry->SetCompressionStream(file, DeflateMethod::Create(), ZipArchiveEntry::CompressionMode::Immediate);
        }
    }
}

bool ZipManager::ZipFolder(const std::string& folderPath, const std::string& zipFilename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        ZipArchive::Ptr archive = ZipFile::Open(zipFilename);
        AddFolderToArchive(folderPath, archive, folderPath);
        ZipFile::SaveAndClose(archive, zipFilename);
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to zip folder: {}", e.what());

        return false;
    }
}
