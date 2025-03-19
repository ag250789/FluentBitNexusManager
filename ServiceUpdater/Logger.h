#ifndef LOGGER_H
#define LOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <chrono>

using json = nlohmann::json;
namespace fs = std::filesystem;

class Logger {
public:
    static void Init();
    static void Shutdown();  
    static std::shared_ptr<spdlog::logger>& GetLogger();

private:
    static std::shared_ptr<spdlog::logger> s_Logger;
    static std::shared_ptr<spdlog::details::thread_pool> s_ThreadPool;  // Thread pool for async logger
    static json LoadConfig();
    static spdlog::level::level_enum GetLogLevel(const std::string& level);
    static void CleanupOldLogs(const std::string& directory, int days, int maxFiles);
};

#define LOG_TRACE(...) SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(Logger::GetLogger().get(), spdlog::level::critical, __VA_ARGS__)

#endif // LOGGER_H
