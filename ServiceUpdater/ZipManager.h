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

/**
 * @class ZipManager
 * @brief Manages ZIP archive operations, including adding, extracting, encrypting, and removing files.
 *
 * This class provides methods for handling ZIP files, including creating archives,
 * adding files, extracting files, encrypting archives, and listing contents.
 * It ensures thread safety using a mutex lock.
 */
class ZipManager {
public:
    ZipManager();
    ~ZipManager();

    /**
     * @brief Adds a file to an existing ZIP archive.
     *
     * This function opens a ZIP archive (or creates one if it doesn't exist),
     * compresses the given file, and adds it under the specified entry name.
     *
     * @param zipFilename The path to the ZIP file.
     * @param fileToAdd The path of the file to add.
     * @param entryName The name under which the file should be stored in the archive.
     * @return true if the file was added successfully, false otherwise.
     */
    bool AddFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName);

    /**
     * @brief Adds an encrypted file to a ZIP archive.
     *
     * This function adds a file to the ZIP archive with password protection.
     *
     * @param zipFilename The path to the ZIP file.
     * @param fileToAdd The file to add to the archive.
     * @param entryName The name under which the file should be stored.
     * @param password The password for encrypting the file.
     * @return true if the file was added successfully, false otherwise.
     */
    bool AddEncryptedFileToArchive(const std::string& zipFilename, const std::string& fileToAdd, const std::string& entryName, const std::string& password);

    /**
     * @brief Extracts a file from a ZIP archive.
     *
     * This function retrieves a file from the ZIP archive and saves it to the specified location.
     *
     * @param zipFilename The path to the ZIP archive.
     * @param entryName The name of the file inside the archive.
     * @param outputFilename The destination path where the file will be extracted.
     * @return true if the file was successfully extracted, false otherwise.
     */
    bool ExtractFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename);

    /**
     * @brief Extracts an encrypted file from a ZIP archive.
     *
     * This function extracts a password-protected file from the archive.
     *
     * @param zipFilename The path to the ZIP file.
     * @param entryName The name of the file to extract.
     * @param outputFilename The destination path for the extracted file.
     * @param password The password to decrypt the file.
     * @return true if extraction is successful, false otherwise.
     */
    bool ExtractEncryptedFileFromArchive(const std::string& zipFilename, const std::string& entryName, const std::string& outputFilename, const std::string& password);

    /**
     * @brief Removes an entry (file or directory) from a ZIP archive.
     *
     * @param zipFilename The path to the ZIP archive.
     * @param entryName The name of the file or directory to remove.
     * @return true if the entry was successfully removed, false otherwise.
     */
    bool RemoveEntryFromArchive(const std::string& zipFilename, const std::string& entryName);

    /**
     * @brief Lists the contents of a ZIP archive.
     *
     * @param zipFilename The path to the ZIP archive.
     * @return A vector of file and directory names inside the ZIP archive.
     */
    std::vector<std::string> ListArchiveContents(const std::string& zipFilename);

    /**
     * @brief Extracts all files from a ZIP archive into a specified folder.
     *
     * This function extracts all files and directories from the archive, preserving their structure.
     *
     * @param zipFilename The path to the ZIP file.
     * @param outputFolder The directory where the archive contents will be extracted.
     * @return true if extraction was successful, false otherwise.
     */
    bool ExtractArchiveToFolder(const std::string& zipFilename, const std::string& outputFolder);

    /**
     * @brief Zips an entire folder into a ZIP archive.
     *
     * This function compresses a directory and its contents into a ZIP file.
     *
     * @param folderPath The path to the folder to compress.
     * @param zipFilename The path to the resulting ZIP file.
     * @return true if the folder was successfully compressed, false otherwise.
     */
    bool ZipFolder(const std::string& folderPath, const std::string& zipFilename);

private:
    std::mutex m_mutex;  ///< Mutex to ensure thread-safe operations.

    /**
     * @brief Adds an entire folder and its contents to a ZIP archive.
     *
     * This function recursively adds all files and subdirectories to the archive.
     *
     * @param folderPath The path to the folder being added.
     * @param archive A pointer to the ZIP archive object.
     * @param basePath The base path to preserve relative file paths.
     */
    

    void AddFolderToArchive(const std::string& folderPath, ZipArchive::Ptr archive, const std::string& basePath);
};

#endif // ZIP_MANAGER_H
