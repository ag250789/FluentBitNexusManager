#include "Logger.h"
#include "UpgradePathManager.h"

std::shared_ptr<spdlog::logger> Logger::s_Logger;
std::shared_ptr<spdlog::details::thread_pool> Logger::s_ThreadPool;  

/**
 * @brief Initializes the logger with settings from a configuration file.
 *
 * This function loads logger configuration from a JSON file, including log directory,
 * file name, maximum file size, log rotation settings, and async logging options.
 * It sets up both console and file log sinks, with automatic log rotation and
 * periodic log flushing.
 */
void Logger::Init() {
    if (s_Logger) {
        return;  
    }

    try {
        json config = LoadConfig();

        std::string logDirectory = config["log_directory"].get<std::string>();
        std::string logFile = config["log_file"].get<std::string>();
        size_t maxFileSize = config["max_file_size"].get<size_t>();
        size_t maxFiles = config["max_files"].get<size_t>();
        bool asyncLogging = config["async_logging"].get<bool>();
        int deleteLogsOlderThanDays = config["delete_logs_older_than_days"].get<int>();
        spdlog::level::level_enum logLevel = GetLogLevel(config["log_level"].get<std::string>());

        if (!fs::exists(logDirectory)) {
            fs::create_directories(logDirectory);
        }

        std::string logPath = logDirectory + "/" + logFile;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] [%s:%#] %v");

        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, maxFileSize, maxFiles, true);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] [%s:%#] %v");

        if (asyncLogging) {
            if (!s_ThreadPool) {
                s_ThreadPool = std::make_shared<spdlog::details::thread_pool>(8192, 1);  // Globalni thread pool
            }
            s_Logger = std::make_shared<spdlog::async_logger>(
                "Logger", spdlog::sinks_init_list{ console_sink, file_sink },
                s_ThreadPool, spdlog::async_overflow_policy::block);
            spdlog::register_logger(s_Logger);
        }
        else {
            s_Logger = std::make_shared<spdlog::logger>("Logger", spdlog::sinks_init_list{ console_sink, file_sink });
        }

        s_Logger->set_level(logLevel);
        s_Logger->flush_on(spdlog::level::err);
        spdlog::flush_every(std::chrono::seconds(5)); 

        CleanupOldLogs(logDirectory, deleteLogsOlderThanDays, maxFiles);

    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }
}

/**
 * @brief Shuts down the logger and ensures all log messages are flushed.
 *
 * This function flushes all pending log messages and then shuts down the logger.
 * A short delay is introduced to ensure all logs are properly written before shutdown.
 */
void Logger::Shutdown() {
    if (s_Logger) {
        s_Logger->flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  
        spdlog::shutdown();
        s_Logger.reset();
    }
}

/**
 * @brief Retrieves the global logger instance.
 *
 * @return A shared pointer to the global logger instance.
 */
std::shared_ptr<spdlog::logger>& Logger::GetLogger() {
    return s_Logger;
}

/**
 * @brief Loads the logger configuration from a JSON file.
 *
 * This function reads logging settings from a JSON configuration file.
 * If the file is missing or invalid, it returns a default configuration.
 *
 * @return A JSON object containing the logger configuration.
 */
json Logger::LoadConfig() {
    UpgradePathManager pathManager;
    std::string logFileName = pathManager.GetLogPath();
    std::string logDirName = pathManager.GetLogDirectory();
    std::string logConfig = pathManager.GetLoggerFilePath();

    json defaultConfig = {
        {"log_level", "info"},
        {"log_directory", logDirName},
        {"log_file", logFileName},
        {"max_file_size", 5242880},
        {"max_files", 3},
        {"async_logging", true},
        {"delete_logs_older_than_days", 7}
    };

    std::ifstream file(logConfig);
    if (!file) {
        return defaultConfig;
    }

    try {
        json config;
        file >> config;
        return config;
    }
    catch (...) {
        return defaultConfig;
    }
}

/**
 * @brief Converts a string representation of a log level into an `spdlog` log level.
 *
 * This function maps string values such as "trace", "debug", "info", etc.,
 * to their corresponding `spdlog` level enumeration values.
 *
 * @param level The log level as a string.
 * @return The corresponding `spdlog::level::level_enum` value.
 */
spdlog::level::level_enum Logger::GetLogLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}


/**
 * @brief Cleans up old log files based on age and maximum file limits.
 *
 * This function deletes log files older than a specified number of days and removes
 * the oldest logs if the total number of log files exceeds the configured limit.
 *
 * @param directory The directory containing the log files.
 * @param days The number of days after which logs should be deleted.
 * @param maxFiles The maximum number of log files to retain.
 */

void Logger::CleanupOldLogs(const std::string& directory, int days, int maxFiles) {
    if (days <= 0 && maxFiles <= 0) return; 

    try {
        auto now = std::chrono::system_clock::now();
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> logFiles;

        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                auto ftime = entry.last_write_time();
                logFiles.emplace_back(entry.path(), ftime);
            }
        }

        for (const auto& [path, ftime] : logFiles) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
            );

            auto age = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count() / 24;

            if (age > days) {
                std::error_code ec;
                fs::remove(path, ec);
                if (ec) {
                    std::cerr << "Failed to delete old log: " << path << " - " << ec.message() << std::endl;
                }
            }
        }

        logFiles.clear();
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                auto ftime = entry.last_write_time();
                logFiles.emplace_back(entry.path(), ftime);
            }
        }

        if (maxFiles > 0 && logFiles.size() > maxFiles) {
            std::sort(logFiles.begin(), logFiles.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });

            size_t numToDelete = logFiles.size() - maxFiles;
            for (size_t i = 0; i < numToDelete; ++i) {
                std::error_code ec;
                fs::remove(logFiles[i].first, ec);
                if (ec) {
                    std::cerr << "Failed to delete excess log: " << logFiles[i].first << " - " << ec.message() << std::endl;
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error while cleaning logs: " << e.what() << std::endl;
    }
}

