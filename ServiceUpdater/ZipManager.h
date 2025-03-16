#ifndef ZIP_MANAGER_H
#define ZIP_MANAGER_H

#include <mutex>
#include <string>
#include <vector>
#include "ZipLib/ZipFile.h"
#include "ZipLib/streams/memstream.h"
#include "ZipLib/methods/DeflateMethod.h"
#include "ZipLib/methods/Bzip2Method.h"
#include <iostream>
#include <fstream>
#include "Logger.h"


class ZipManager {
public:
    ZipManager();
    ~ZipManager();

    bool AddFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName = "");
    bool AddEncryptedFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName, const std::string& password);
    bool ExtractFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename);
    bool ExtractEncryptedFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename, const std::string& password);
    bool RemoveEntryFromArchive(const std::string& zipFilename, const std::string& entryName);
    std::vector<std::string> ListArchiveContents(const std::string& zipFilename);

    // Nove funkcije
    bool ExtractArchiveToFolder(const std::string& zipFilename, const std::string& outputFolder);
    bool ZipFolder(const std::string& folderPath, const std::string& zipFilename);

private:
    std::mutex m_mutex;

    void AddFolderToArchive(const std::string& folderPath, ZipArchive::Ptr archive, const std::string& basePath);
};

#endif // ZIP_MANAGER_H
