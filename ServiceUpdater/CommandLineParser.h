#ifndef COMMAND_LINE_PARSER_H
#define COMMAND_LINE_PARSER_H

#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <boost/program_options.hpp>
#include <nlohmann/json.hpp> 
#include <spdlog/spdlog.h>
#include <unordered_set>
#include <filesystem>
#include <regex>


namespace po = boost::program_options;
using json = nlohmann::json;
namespace fs = std::filesystem;


class CommandLineParser {
public:
    /**
     * @brief Constructor for parsing command-line arguments.
     * Initializes and validates command-line options such as company ID, region, and site ID.
     *
     * @param argc Argument count from the main function.
     * @param argv Argument vector from the main function.
     */
    CommandLineParser(int argc, char** argv) {
        // Define command-line options (no default values, all are required)
        desc.add_options()
            ("help", "Produce help message")
            ("companyid", po::value<std::string>(&companyId)->required(), "Company ID")
            ("region", po::value<std::string>(&region)->required(), "Region")
            ("siteid", po::value<std::string>(&siteId)->required(), "Site ID")
            ("log_config", po::value<std::string>(&logPath)->default_value(""), "Log Config path (optional)")
            ("proxy_config", po::value<std::string>(&proxyConfig)->default_value(""), "Proxy configuration file path (optional)")
            ("crontab", po::value<std::string>(&cronTab)->default_value(""), "Cron Expression (optional)");

        // Parse and validate command-line options
        try {
            po::store(po::parse_command_line(argc, argv, desc), vm);
            po::notify(vm); // Validate required options
        }
        catch (const po::error& e) {
            // If error occurs, print error and display help message
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << desc << std::endl;
            throw std::runtime_error("Invalid command-line arguments");
        }
    }

    ~CommandLineParser() {
    }

    /**
     * @brief Parses the command-line arguments and validates region.
     * Also checks if the user requested help.
     *
     * @return true if parsing was successful, false otherwise.
     */
    bool parse() {
        try {
            // If 'help' option is passed, display help and exit
            if (vm.count("help")) {
                std::cout << desc << std::endl;
                return false;
            }
            validateRegion();
            validateCronTab();

        }
        catch (const po::error& e) {
            spdlog::error("Error: {}", e.what());
            return false;
        }
        return true;
    }

    bool saveControllerConfigToFile(const std::string& filePath) {

        std::filesystem::path dir = std::filesystem::path(filePath).parent_path();
        if (!std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            }
            catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "Error: Could not create directory " << dir << ". " << e.what() << std::endl;
                return false;
            }
        }

        std::transform(region.begin(), region.end(), region.begin(), ::tolower);
        region = CapitalizeFirstLetter(region);

        json config;
        config["CompanyID"] = companyId;
        config["Region"] = region;
        config["SiteID"] = siteId;
        spdlog::info("Configuration for company ID: {}, region: {}, and site ID: {} has been successfully generated.", companyId, region, siteId);


        std::ofstream configFile(filePath);
        if (!configFile.is_open()) {
            spdlog::error("Error: Could not open configuration file for writing: {}", filePath);

            return false;
        }

        configFile << config.dump(4);
        configFile.close();


        return true;
    }
    /**
     * @brief Saves configuration details to a JSON file.
     * This method will create the directory structure if needed.
     *
     * @param filePath The path where the configuration file should be saved.
     * @return true if the file was successfully saved, false otherwise.
     */
    bool saveConfigToFile(const std::string& filePath) {

        std::filesystem::path dir = std::filesystem::path(filePath).parent_path();
        if (!std::filesystem::exists(dir)) {
            try {
                std::filesystem::create_directories(dir);
            }
            catch (const std::filesystem::filesystem_error& e) {
                std::cerr << "Error: Could not create directory " << dir << ". " << e.what() << std::endl;
                return false;
            }
        }

        std::transform(region.begin(), region.end(), region.begin(), ::tolower);
        region = CapitalizeFirstLetter(region);

        json config;
        config["CompanyID"] = companyId;
        config["Region"] = region;
        config["SiteID"] = siteId;
        spdlog::info("Configuration for company ID: {}, region: {}, and site ID: {} has been successfully generated.", companyId, region, siteId);

        if (!logPath.empty()) config["LogConfig"] = logPath;
        if (!proxyConfig.empty()) config["ProxyConfig"] = proxyConfig;
        if (!cronTab.empty()) config["CronTab"] = cronTab;


        std::ofstream configFile(filePath);
        if (!configFile.is_open()) {
            spdlog::error("Error: Could not open configuration file for writing: {}", filePath);


            return false;
        }

        configFile << config.dump(4);
        configFile.close();


        return true;
    }


    // Accessor functions for the parsed parameters
    std::string getCompanyId() const { return companyId; }
    std::string getRegion() const { return region; }
    std::string getSiteId() const { return siteId; }
    std::string getLogPath() const { return logPath; }
    std::string getProxyConfig() const { return proxyConfig; }
    std::string getCronTab() const { return cronTab; }



    static bool LoadConfigFromFile(const std::string& configFilePath, std::string& companyId, std::string& region, std::string& siteId, std::string& logPath, std::string& proxyConfig, std::string& cronTab) {
        if (!fs::exists(configFilePath) || fs::is_empty(configFilePath)) {
            spdlog::error("Configuration file does not exist or is empty: {}", configFilePath);

            return false;
        }

        std::ifstream configFile(configFilePath);
        if (!configFile.is_open()) {
            spdlog::error("Could not open configuration file: {}", configFilePath);

            return false;
        }

        try {
            json config;
            configFile >> config;

            if (config.contains("CompanyID") && config["CompanyID"].is_string()) {
                companyId = config["CompanyID"].get<std::string>();
            }
            else {
                throw std::runtime_error("Invalid or missing 'CompanyID' in configuration file.");
            }

            if (config.contains("Region") && config["Region"].is_string()) {
                region = config["Region"].get<std::string>();
            }
            else {
                throw std::runtime_error("Invalid or missing 'Region' in configuration file.");
            }

            if (config.contains("SiteID") && config["SiteID"].is_string()) {
                siteId = config["SiteID"].get<std::string>();
            }
            else {
                throw std::runtime_error("Invalid or missing 'SiteID' in configuration file.");
            }

            // U?itaj opcione vrednosti ako postoje, u suprotnom ostavi podrazumevanu praznu vrednost
            logPath = config.value("LogConfig", "");
            proxyConfig = config.value("ProxyConfig", "");
            cronTab = config.value("CronTab", "");

        }
        catch (const json::parse_error& e) {
            spdlog::error("Error parsing JSON configuration: {}", e.what());

            return false;
        }
        catch (const std::exception& e) {
            spdlog::error("Error processing configuration file: {}", e.what());

            return false;
        }

        return true;
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

private:
    po::options_description desc{ "Allowed options" };  // Command-line option descriptions
    po::variables_map vm;  // Stores the parsed command-line values
    std::string companyId;  // Company ID from command-line input
    std::string region;     // Region from command-line input
    std::string siteId;     // Site ID from command-line input
    std::string logPath;
    std::string proxyConfig;
    std::string cronTab;

    // Allowed regions
    const std::unordered_set<std::string> validRegions = { "prep", "americas", "europe", "apac", "proba" };
    const std::unordered_set<std::string> testRegions = { "proba" };

    /**
     * @brief Validates if the region provided by the user is valid.
     * Throws an exception if the region is invalid.
     */
    void validateRegion() {
        std::transform(region.begin(), region.end(), region.begin(), ::tolower);

        if (validRegions.find(region) == validRegions.end()) {
            throw std::runtime_error("Error: Region '" + region + "' is not allowed. Allowed regions are: Prep, Americas, Europe, Apac.");
        }

        if (region == "prep" || region == "proba" || region == "apac" || region == "americas" || region == "europe") {
            spdlog::warn("The DCS Streamin Agent Installer is currently being tested for the 'Prep', 'Apac', 'Americas' and 'Europe' region. It will not be used for any other regions until confirmed that it works properly.");

        }
        else if (testRegions.find(region) != testRegions.end()) {
            throw std::runtime_error("Installer is not available for the '" + CapitalizeFirstLetter(region) + "' region at this time. Please wait until it is confirmed to work properly.");
        }

        region = CapitalizeFirstLetter(region);
    }

    
    void validateCronTab() {
        if (cronTab.empty()) {
            return;  // If no cron expression is provided, validation is not needed.
        }

        // Check if the cron expression starts with '@' (indicating a special token)
        static const std::unordered_set<std::string> allowedTokens = {
            "@yearly", "@annually", "@monthly", "@weekly", "@daily", "@hourly"
        };
        if (cronTab[0] == '@') {
            if (allowedTokens.find(cronTab) == allowedTokens.end()) {
                throw std::runtime_error("Invalid cron token: " + cronTab);
            }
            return; // Valid special token, no further validation needed.
        }

        // Split the expression into fields
        std::istringstream iss(cronTab);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }

        if (tokens.size() != 6) {
            throw std::runtime_error("Cron expression must contain exactly 6 fields, but got " + std::to_string(tokens.size()));
        }

        // Allowed ranges for each field
        static const std::vector<std::pair<int, int>> validRanges = {
            {0, 59},  // Seconds
            {0, 59},  // Minutes
            {0, 23},  // Hours
            {1, 31},  // Day of month (1-31 or '?')
            {1, 12},  // Month (1-12 or JAN-DEC)
            {0, 6}    // Day of week (0-6 or SUN-SAT or '?')
        };

        // Allowed month and day-of-week names
        static const std::unordered_set<std::string> monthNames = {
            "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
        };
        static const std::unordered_set<std::string> dayNames = {
            "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
        };

        // Validate each field
        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string& field = tokens[i];
            const auto& range = validRanges[i];

            if (field == "*" || field == "?") {
                // '*' (every value) and '?' (ignore) are always valid
                continue;
            }

            if (i == 4 && monthNames.count(field)) {
                // Valid month name (JAN-DEC)
                continue;
            }

            if (i == 5 && dayNames.count(field)) {
                // Valid day name (SUN-SAT)
                continue;
            }

            // Validate numbers and valid cron patterns
            std::regex validPattern(R"(^(\d+|\d+-\d+|\*/\d+|\d+(,\d+)*)$)");
            if (!std::regex_match(field, validPattern)) {
                throw std::runtime_error("Invalid cron field: " + field + " in position " + std::to_string(i + 1));
            }

            // Validate numeric values inside the field
            std::vector<std::string> parts;
            std::stringstream ss(field);
            std::string part;

            while (std::getline(ss, part, ',')) {
                if (part.find('-') != std::string::npos) {
                    // Range validation (e.g., 1-5)
                    int start, end;
                    if (sscanf(part.c_str(), "%d-%d", &start, &end) != 2 || start > end || start < range.first || end > range.second) {
                        throw std::runtime_error("Invalid range: " + part + " in position " + std::to_string(i + 1));
                    }
                }
                else if (part.find("*/") != std::string::npos) {
                    // Step validation (e.g., */5)
                    int step;
                    if (sscanf(part.c_str(), "*/%d", &step) != 1 || step < 1 || step > range.second) {
                        throw std::runtime_error("Invalid step value: " + part + " in position " + std::to_string(i + 1));
                    }
                }
                else {
                    // Single number validation
                    int value = std::stoi(part);
                    if (value < range.first || value > range.second) {
                        throw std::runtime_error("Invalid value: " + part + " in position " + std::to_string(i + 1));
                    }
                }
            }
        }
    }
    /**
     * @brief Returns the current time as a string in the format YYYY-MM-DD HH:MM:SS.
     * This is mainly used for logging purposes.
     *
     * @return std::string Current time formatted as YYYY-MM-DD HH:MM:SS.
     */

    std::string getCurrentTime() const {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* parts = std::localtime(&now_c);
        std::ostringstream oss;
        oss << std::put_time(parts, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    /**
     * @brief Capitalizes the first letter of the input string.
     *
     * @param word The input string.
     * @return std::string The capitalized string.
     */

    std::string CapitalizeFirstLetter(const std::string& word) {
        if (word.empty()) {
            return word;
        }

        std::string capitalizedWord = word;
        capitalizedWord[0] = std::toupper(capitalizedWord[0]);
        return capitalizedWord;
    }
};


#endif // COMMAND_LINE_PARSER_H
