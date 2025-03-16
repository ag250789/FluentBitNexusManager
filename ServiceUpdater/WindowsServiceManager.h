#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "Logger.h"
#include <mutex>  // For std::recursive_mutex

// Structure that holds basic information about a service.
struct ServiceInfo {
    std::wstring serviceName;   // Unique service name
    std::wstring displayName;   // Display name of the service
    std::wstring binaryPath;    // Path to the service executable
    DWORD serviceType;          // Service type (e.g. SERVICE_WIN32_OWN_PROCESS)
    DWORD startType;            // Startup type (e.g. SERVICE_AUTO_START, SERVICE_DEMAND_START)
    std::wstring dependencies;  // Dependencies (if any)
    std::wstring account;       // Account under which the service runs (if applicable)
    std::wstring password;      // Password for the account (if applicable)
};

// RAII class that manages an SC_HANDLE resource.
class ServiceHandle {
private:
    SC_HANDLE handle;
public:
    explicit ServiceHandle(SC_HANDLE h = nullptr);
    ~ServiceHandle();

    // Disable copying.
    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;

    // Enable moving.
    ServiceHandle(ServiceHandle&& other) noexcept;
    ServiceHandle& operator=(ServiceHandle&& other) noexcept;

    SC_HANDLE get() const;
    bool valid() const;
};

// Class for managing Windows services – supports operations on multiple services.
// This class is thread-safe using a recursive mutex.
class WindowsServiceManager {
private:
    ServiceHandle scmHandle; // Handle for the Service Control Manager (SCM)
    std::map<std::wstring, ServiceInfo> registeredServices; // Registered services container
    mutable std::recursive_mutex mtx; // Recursive mutex to protect shared state

    // Helper method to open a service handle with the desired access rights.
    ServiceHandle openServiceHandle(const std::wstring& serviceName, DWORD desiredAccess) const;

public:
    // Constructor that opens a handle to the SCM with the given desired access.
    WindowsServiceManager(DWORD dwDesiredAccess = SC_MANAGER_ALL_ACCESS);
    ~WindowsServiceManager() = default;

    // Registers a service in the internal container (for tracking purposes only, does not install the service).
    void registerService(const ServiceInfo& serviceInfo);


    // Unregisters a service from the internal container.
    void unregisterService(const std::wstring& serviceName);

    // Installs a service using the provided ServiceInfo data.
    void installService(const ServiceInfo& serviceInfo);
    // Installs a registered service by its name.
    void installService(const std::wstring& serviceName);

    // Batch installation of all registered services.
    void installAllServices();

    // Removes (uninstalls) a service.
    void removeService(const std::wstring& serviceName);
    // Batch removal of all registered services.
    void removeAllServices();

    // Starts a service with optional arguments.
    bool startService(const std::wstring& serviceName, const std::vector<std::wstring>& args = {});
    // Batch start of all registered services.
    void startAllServices();

    // Stops a service.
    bool stopService(const std::wstring& serviceName);
    // Batch stop of all registered services.
    void stopAllServices();

    // Pauses a service.
    bool pauseService(const std::wstring& serviceName);

    // Continues a paused service.
    void continueService(const std::wstring& serviceName);

    // Queries the status of a service.
    SERVICE_STATUS queryServiceStatus(const std::wstring& serviceName);
    // Batch query of the status of all registered services – returns a map of (service name, status).
    std::map<std::wstring, SERVICE_STATUS> queryAllServicesStatus();

    // Restarts a service: stops and then starts the service.
    // Optional arguments can be provided for starting the service.
    void restartService(const std::wstring& serviceName, const std::vector<std::wstring>& args = {});

    // Batch restart of all registered services.
    void restartAllServices();

    // Checks if a service is installed.
    bool isServiceInstalled(const std::wstring& serviceName);

    // Checks if a service is running.
    bool isServiceRunning(const std::wstring& serviceName);
};

// Helper functions for converting between wide strings and UTF-8 strings.
std::string ConvertWStringToString(const std::wstring& wstr);
std::wstring ConvertStringToWString(const std::string& str);
std::wstring ServiceStatusToString(DWORD state);
