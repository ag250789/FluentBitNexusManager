#include "WindowsServiceManager.h"
#include <stdexcept>
#include <vector>
#include <Windows.h>
#include <iostream>
#include <string>
#include <map>
#include <filesystem>
#include <algorithm>
#include <mutex>

namespace fs = std::filesystem;

// ---------------------------
// Implementation of ServiceHandle
// ---------------------------

ServiceHandle::ServiceHandle(SC_HANDLE h) : handle(h) {}

ServiceHandle::~ServiceHandle() {
    if (handle) {
        CloseServiceHandle(handle);
    }
}

ServiceHandle::ServiceHandle(ServiceHandle&& other) noexcept : handle(other.handle) {
    other.handle = nullptr;
}

ServiceHandle& ServiceHandle::operator=(ServiceHandle&& other) noexcept {
    if (this != &other) {
        if (handle) {
            CloseServiceHandle(handle);
        }
        handle = other.handle;
        other.handle = nullptr;
    }
    return *this;
}

SC_HANDLE ServiceHandle::get() const {
    return handle;
}

bool ServiceHandle::valid() const {
    return handle != nullptr;
}

// ---------------------------
// Helper functions for string conversion
// ---------------------------

std::string ConvertWStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, nullptr, nullptr);
    return str;
}


std::wstring ConvertStringToWString(const std::string& str) {
    if (str.empty()) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    if (size_needed <= 0) return {};
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], size_needed);
    return wstr;
}

// ---------------------------
// Implementation of WindowsServiceManager
// ---------------------------

WindowsServiceManager::WindowsServiceManager(DWORD dwDesiredAccess) {
    scmHandle = ServiceHandle(OpenSCManager(nullptr, nullptr, dwDesiredAccess));
    if (!scmHandle.valid()) {
        throw std::runtime_error("Failed to open Service Control Manager.");
    }
}

ServiceHandle WindowsServiceManager::openServiceHandle(const std::wstring& serviceName, DWORD desiredAccess) const {
    if (!scmHandle.valid()) {
        throw std::runtime_error("SCM handle is not valid.");
    }
    ServiceHandle serviceHandle(OpenService(scmHandle.get(), serviceName.c_str(), desiredAccess));
    if (!serviceHandle.valid()) {
        DWORD error = GetLastError();
        throw std::runtime_error("Failed to open service: " + ConvertWStringToString(serviceName) +
            " (Error: " + std::to_string(error) + ")");
    }
    return serviceHandle;
}

void WindowsServiceManager::registerService(const ServiceInfo& serviceInfo) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    registeredServices[serviceInfo.serviceName] = serviceInfo;
}

void WindowsServiceManager::unregisterService(const std::wstring& serviceName) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    auto it = registeredServices.find(serviceName);
    if (it != registeredServices.end()) {
        registeredServices.erase(it);
    }
}

void WindowsServiceManager::installService(const ServiceInfo& serviceInfo) {
    ServiceHandle serviceHandle(
        CreateService(
            scmHandle.get(),                     // SCM handle
            serviceInfo.serviceName.c_str(),     // Service name
            serviceInfo.displayName.c_str(),     // Display name
            SERVICE_ALL_ACCESS,                  // Access rights
            serviceInfo.serviceType,             // Service type
            serviceInfo.startType,               // Startup type
            SERVICE_ERROR_NORMAL,                // Error control
            serviceInfo.binaryPath.c_str(),      // Binary path
            nullptr,                             // Service group
            nullptr,                             // Reserved
            serviceInfo.dependencies.empty() ? nullptr : serviceInfo.dependencies.c_str(),
            serviceInfo.account.empty() ? nullptr : serviceInfo.account.c_str(),
            serviceInfo.password.empty() ? nullptr : serviceInfo.password.c_str()
        )
    );
    if (!serviceHandle.valid()) {
        DWORD error = GetLastError();
        throw std::runtime_error("Failed to install service: " + ConvertWStringToString(serviceInfo.serviceName) +
            " (Error: " + std::to_string(error) + ")");
    }
    // After successful installation, register the service in the internal container.
    registerService(serviceInfo);
}

void WindowsServiceManager::installService(const std::wstring& serviceName) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    auto it = registeredServices.find(serviceName);
    if (it == registeredServices.end()) {
        throw std::runtime_error("Service is not registered: " + ConvertWStringToString(serviceName));
    }
    installService(it->second);
}

void WindowsServiceManager::installAllServices() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            installService(pair.second);
        }
        catch (const std::exception&) {
            // Log error and continue with the next service.
        }
    }
}

void WindowsServiceManager::removeService(const std::wstring& serviceName) {
    try {
        // First, query the current status of the service.
        SERVICE_STATUS status = queryServiceStatus(serviceName);

        // If the service is not already stopped, attempt to stop it.
        if (status.dwCurrentState != SERVICE_STOPPED) {
            //spdlog::info("Service '{}' is running. Attempting to stop it before removal...", ConvertWStringToString(serviceName));
            LOG_INFO("Service '{}' is running. Attempting to stop it before removal...", ConvertWStringToString(serviceName));

            try {
                stopService(serviceName);
            }
            catch (const std::exception& ex) {
                // If stopping fails, output the error but continue to poll for stopped status.
                //spdlog::warn("Warning: Failed to stop service '{}': {}", ConvertWStringToString(serviceName), ex.what());
                LOG_WARN("Warning: Failed to stop service '{}': {}", ConvertWStringToString(serviceName), ex.what());
            }

            // Poll until the service is confirmed stopped (up to 30 seconds).
            const int maxWaitAttempts = 60;  // 60 attempts * 500ms = 30 seconds max wait
            int attempts = 0;
            while (attempts < maxWaitAttempts) {
                status = queryServiceStatus(serviceName);
                if (status.dwCurrentState == SERVICE_STOPPED) {
                    break;
                }
                Sleep(500); // wait for 500 milliseconds before polling again
                ++attempts;
            }
            if (attempts == maxWaitAttempts) {
                //spdlog::error("Timeout waiting for service '{}' to stop before removal.", ConvertWStringToString(serviceName));
                LOG_ERROR("Timeout waiting for service '{}' to stop before removal.", ConvertWStringToString(serviceName));
                return;
            }
        }

        // Now that the service is stopped, open its handle for deletion.
        ServiceHandle serviceHandle = openServiceHandle(serviceName, DELETE);
        if (!DeleteService(serviceHandle.get())) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to remove service '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to remove service '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            return;
        }

        // Finally, unregister the service from our internal container.
        unregisterService(serviceName);
        //spdlog::info("Service '{}' has been successfully removed.", ConvertWStringToString(serviceName));
        LOG_INFO("Service '{}' has been successfully removed.", ConvertWStringToString(serviceName));
    }
    catch (const std::exception& e) {
        //spdlog::error("Exception occurred while removing service '{}': {}", ConvertWStringToString(serviceName), e.what());
        LOG_ERROR("Exception occurred while removing service '{}': {}", ConvertWStringToString(serviceName), e.what());
    }
    catch (...) {
        //spdlog::error("Unknown error occurred while removing service '{}'.", ConvertWStringToString(serviceName));
        LOG_ERROR("Unknown error occurred while removing service '{}'.", ConvertWStringToString(serviceName));
    }
}


void WindowsServiceManager::removeAllServices() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            removeService(pair.first);
        }
        catch (const std::exception&) {
            // Log error and continue.
        }
    }
}

bool WindowsServiceManager::startService(const std::wstring& serviceName, const std::vector<std::wstring>& args) {
    try {
        ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_START);

        if (!serviceHandle.get()) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to open service handle for '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to open service handle for '{}'. Error: {}", ConvertWStringToString(serviceName), error);

            return false;
        }

        std::vector<LPCTSTR> argPtrs;
        for (const auto& arg : args) {
            argPtrs.push_back(arg.c_str());
        }

        if (!::StartService(serviceHandle.get(), static_cast<DWORD>(argPtrs.size()), argPtrs.empty() ? nullptr : argPtrs.data())) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to start service '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to start service '{}'. Error: {}", ConvertWStringToString(serviceName), error);

            return false;
        }

        //spdlog::info("Service '{}' started successfully.", ConvertWStringToString(serviceName));
        LOG_INFO("Service '{}' started successfully.", ConvertWStringToString(serviceName));

        return true;

    }
    catch (const std::exception& e) {
        //spdlog::error("Exception occurred while starting service '{}': {}", ConvertWStringToString(serviceName), e.what());
        LOG_ERROR("Exception occurred while starting service '{}': {}", ConvertWStringToString(serviceName), e.what());

        return false;
    }
    catch (...) {
        //spdlog::error("Unknown error occurred while starting service '{}'.", ConvertWStringToString(serviceName));
        LOG_ERROR("Unknown error occurred while starting service '{}'.", ConvertWStringToString(serviceName));

        return false;
    }
}

void WindowsServiceManager::startAllServices() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            startService(pair.first);
        }
        catch (const std::exception&) {
            // Log error and continue with the next service.
        }
    }
}

bool WindowsServiceManager::stopService(const std::wstring& serviceName) {
    try {
        ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);

        if (!serviceHandle.get()) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to open service handle for '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to open service handle for '{}'. Error: {}", ConvertWStringToString(serviceName), error);

            return false;
        }

        SERVICE_STATUS_PROCESS ssp;
        DWORD bytesNeeded;

        // Proveravamo trenutni status servisa
        if (!QueryServiceStatusEx(serviceHandle.get(), SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&ssp), sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to query service status for '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to query service status for '{}'. Error: {}", ConvertWStringToString(serviceName), error);

            return false;
        }

        // Ako je servis ve? zaustavljen, nema potrebe za daljim akcijama
        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            //spdlog::info("Service '{}' is already stopped.", ConvertWStringToString(serviceName));
            LOG_INFO("Service '{}' is already stopped.", ConvertWStringToString(serviceName));

            return true;
        }

        // Pokušavamo da zaustavimo servis
        //spdlog::info("Stopping service '{}'...", ConvertWStringToString(serviceName));
        LOG_INFO("Stopping service '{}'...", ConvertWStringToString(serviceName));

        SERVICE_STATUS status;
        if (!ControlService(serviceHandle.get(), SERVICE_CONTROL_STOP, &status)) {
            DWORD error = GetLastError();
            //spdlog::error("Failed to send stop command to service '{}'. Error: {}", ConvertWStringToString(serviceName), error);
            LOG_ERROR("Failed to send stop command to service '{}'. Error: {}", ConvertWStringToString(serviceName), error);

            return false;
        }

        // ?ekamo da se servis zaista ugasi
        const int maxRetries = 10; // Maksimalan broj provera
        const int waitTimeMs = 500; // Vreme ?ekanja izme?u provera u milisekundama
        int retries = 0;

        while (ssp.dwCurrentState != SERVICE_STOPPED && retries < maxRetries) {
            Sleep(waitTimeMs);
            if (!QueryServiceStatusEx(serviceHandle.get(), SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&ssp), sizeof(SERVICE_STATUS_PROCESS), &bytesNeeded)) {
                DWORD error = GetLastError();
                //spdlog::error("Failed to query service status during stop process for '{}'. Error: {}", ConvertWStringToString(serviceName), error);
                LOG_ERROR("Failed to query service status during stop process for '{}'. Error: {}", ConvertWStringToString(serviceName), error);

                return false;
            }
            retries++;
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED) {
            //spdlog::info("Service '{}' stopped successfully.", ConvertWStringToString(serviceName));
            LOG_INFO("Service '{}' stopped successfully.", ConvertWStringToString(serviceName));

            return true;
        }
        else {
            //spdlog::error("Service '{}' did not stop within the expected time.", ConvertWStringToString(serviceName));
            LOG_ERROR("Service '{}' did not stop within the expected time.", ConvertWStringToString(serviceName));

            return false;
        }
    }
    catch (const std::exception& e) {
        //spdlog::error("Exception occurred while stopping service '{}': {}", ConvertWStringToString(serviceName), e.what());
        LOG_ERROR("Exception occurred while stopping service '{}': {}", ConvertWStringToString(serviceName), e.what());

        return false;
    }
    catch (...) {
        //spdlog::error("Unknown error occurred while stopping service '{}'.", ConvertWStringToString(serviceName));
        LOG_ERROR("Unknown error occurred while stopping service '{}'.", ConvertWStringToString(serviceName));

        return false;
    }
}


void WindowsServiceManager::stopAllServices() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            stopService(pair.first);
        }
        catch (const std::exception&) {
            // Log error and continue with the next service.
        }
    }
}

bool WindowsServiceManager::pauseService(const std::wstring& serviceName) {
    ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_PAUSE_CONTINUE);
    SERVICE_STATUS status;
    if (!ControlService(serviceHandle.get(), SERVICE_CONTROL_PAUSE, &status)) {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_SERVICE_CONTROL) {
            throw std::runtime_error("Service does not support PAUSE: " + ConvertWStringToString(serviceName));
        }
        else {
            throw std::runtime_error("Failed to pause service: " + ConvertWStringToString(serviceName) +
                " (Error: " + std::to_string(error) + ")");
        }
    }
    return true;
}

void WindowsServiceManager::continueService(const std::wstring& serviceName) {
    ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_PAUSE_CONTINUE | SERVICE_QUERY_STATUS);
    SERVICE_STATUS status;
    if (!ControlService(serviceHandle.get(), SERVICE_CONTROL_CONTINUE, &status)) {
        DWORD error = GetLastError();
        throw std::runtime_error("Failed to continue service: " + ConvertWStringToString(serviceName) +
            " (Error: " + std::to_string(error) + ")");
    }
}

// Converts a service state code (dwCurrentState) to a human-readable string.
std::wstring ServiceStatusToString(DWORD state) {
    switch (state) {
    case SERVICE_STOPPED:
        return L"Stopped";
    case SERVICE_START_PENDING:
        return L"Start Pending";
    case SERVICE_STOP_PENDING:
        return L"Stop Pending";
    case SERVICE_RUNNING:
        return L"Running";
    case SERVICE_CONTINUE_PENDING:
        return L"Continue Pending";
    case SERVICE_PAUSE_PENDING:
        return L"Pause Pending";
    case SERVICE_PAUSED:
        return L"Paused";
    default:
        return L"Unknown";
    }
}

SERVICE_STATUS WindowsServiceManager::queryServiceStatus(const std::wstring& serviceName) {
    ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_QUERY_STATUS);
    SERVICE_STATUS status;
    if (!QueryServiceStatus(serviceHandle.get(), &status)) {
        DWORD error = GetLastError();
        throw std::runtime_error("Failed to query service status: " + ConvertWStringToString(serviceName) +
            " (Error: " + std::to_string(error) + ")");
    }
    return status;
}

std::map<std::wstring, SERVICE_STATUS> WindowsServiceManager::queryAllServicesStatus() {
    std::map<std::wstring, SERVICE_STATUS> statuses;
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            statuses[pair.first] = queryServiceStatus(pair.first);
        }
        catch (const std::exception&) {
            // Skip service or log error.
        }
    }
    return statuses;
}

// ---------------------------
// Restart service functions
// ---------------------------

void WindowsServiceManager::restartService(const std::wstring& serviceName, const std::vector<std::wstring>& args) {
    // Query the current service status.
    SERVICE_STATUS currentStatus = queryServiceStatus(serviceName);

    // If the service is not stopped, attempt to stop it.
    if (currentStatus.dwCurrentState != SERVICE_STOPPED) {
        const int maxStopAttempts = 10;
        int stopAttempt = 0;
        bool stopSucceeded = false;

        while (stopAttempt < maxStopAttempts && !stopSucceeded) {
            try {
                // Attempt to stop the service.
                stopService(serviceName);
                stopSucceeded = true;
            }
            catch (const std::runtime_error& ex) {
                // Get the error code from the exception message by re-querying the service status.
                SERVICE_STATUS statusAfter = queryServiceStatus(serviceName);
                if (statusAfter.dwCurrentState == SERVICE_STOPPED) {
                    // The service is already stopped.
                    stopSucceeded = true;
                    break;
                }
                else {
                    // Extract error message for analysis.
                    std::string errStr = ex.what();
                    // If the error indicates that the service cannot accept control messages (1061)
                    // or that the service is not active (1062), then wait and try again.
                    if (errStr.find("1061") != std::string::npos || errStr.find("1062") != std::string::npos) {
                        Sleep(500);
                        ++stopAttempt;
                    }
                    else {
                        // Rethrow any other error.
                        throw;
                    }
                }
            }
        }
        if (!stopSucceeded) {
            throw std::runtime_error("Failed to stop service after multiple attempts: " + ConvertWStringToString(serviceName));
        }

        // Poll until the service is confirmed stopped.
        const int maxWaitAttempts = 60;  // 60 * 500ms = 30 seconds maximum wait time.
        int waitAttempts = 0;
        while (waitAttempts < maxWaitAttempts) {
            SERVICE_STATUS status = queryServiceStatus(serviceName);
            if (status.dwCurrentState == SERVICE_STOPPED) {
                break;
            }
            Sleep(500);
            ++waitAttempts;
        }
        if (waitAttempts == maxWaitAttempts) {
            throw std::runtime_error("Timeout waiting for service to stop: " + ConvertWStringToString(serviceName));
        }
    }
    // At this point, the service is confirmed stopped; start the service with optional arguments.
    startService(serviceName, args);
}



void WindowsServiceManager::restartAllServices() {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    for (const auto& pair : registeredServices) {
        try {
            restartService(pair.first);
        }
        catch (const std::exception&) {
            // Log error and continue with the next service.
        }
    }
}

// ---------------------------
// Implementation of isServiceInstalled
// ---------------------------
bool WindowsServiceManager::isServiceInstalled(const std::wstring& serviceName) {
    std::lock_guard<std::recursive_mutex> lock(mtx);

    try {
        ServiceHandle serviceHandle = openServiceHandle(serviceName, SERVICE_QUERY_STATUS);
        if (!serviceHandle.valid()) {
            //spdlog::warn("Service '{}' is NOT installed (handle is invalid).", ConvertWStringToString(serviceName));
            LOG_WARN("Service '{}' is NOT installed (handle is invalid).", ConvertWStringToString(serviceName));

            return false;
        }
        return true;
    }
    catch (const std::exception& e) {
        //spdlog::error("Exception while checking if service '{}' is installed: {}", ConvertWStringToString(serviceName), e.what());
        LOG_ERROR("Exception while checking if service '{}' is installed: {}", ConvertWStringToString(serviceName), e.what());

        return false;
    }
}


// ---------------------------
// Implementation of isServiceRunning
// ---------------------------
bool WindowsServiceManager::isServiceRunning(const std::wstring& serviceName) {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    try {
        SERVICE_STATUS status = queryServiceStatus(serviceName);
        return status.dwCurrentState == SERVICE_RUNNING;
    }
    catch (const std::exception&) {
        return false;
    }
}


