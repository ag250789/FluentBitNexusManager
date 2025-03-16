#include "Logger.h"
#include "UpgradePathManager.h"

std::shared_ptr<spdlog::logger> Logger::s_Logger;
std::shared_ptr<spdlog::details::thread_pool> Logger::s_ThreadPool;  // Globalni thread pool za async logger

void Logger::Init() {
    if (s_Logger) {
        return;  // Logger je ve? inicijalizovan, ne dupliramo ga
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

        // ? Poboljšana rotacija logova
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
        spdlog::flush_every(std::chrono::seconds(5));  // ? Automatsko flushovanje svakih 5 sekundi

        CleanupOldLogs(logDirectory, deleteLogsOlderThanDays, maxFiles);

    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
    }
}

void Logger::Shutdown() {
    if (s_Logger) {
        s_Logger->flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Pauza da osigura flush pre gašenja
        spdlog::shutdown();
        s_Logger.reset();
    }
}

std::shared_ptr<spdlog::logger>& Logger::GetLogger() {
    return s_Logger;
}

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

spdlog::level::level_enum Logger::GetLogLevel(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}



void Logger::CleanupOldLogs(const std::string& directory, int days, int maxFiles) {
    if (days <= 0 && maxFiles <= 0) return; // Nema potrebe za ?iš?enjem

    try {
        auto now = std::chrono::system_clock::now();
        std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> logFiles;

        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                auto ftime = entry.last_write_time();
                logFiles.emplace_back(entry.path(), ftime);
            }
        }

        // 1. Brisanje po starosti (delete_logs_older_than_days)
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

        // Ponovo u?itaj sve logove nakon brisanja starih fajlova
        logFiles.clear();
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                auto ftime = entry.last_write_time();
                logFiles.emplace_back(entry.path(), ftime);
            }
        }

        // 2. Brisanje po max_files - Ako imamo više fajlova nego dozvoljeno, brišemo najstarije
        if (maxFiles > 0 && logFiles.size() > maxFiles) {
            std::sort(logFiles.begin(), logFiles.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; }); // Sortiraj po starosti

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

