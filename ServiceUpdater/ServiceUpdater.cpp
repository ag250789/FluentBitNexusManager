#include <iostream>
#include <filesystem>
#include <cassert>
#include <windows.h>
#include "MainService.h"
#include "Logger.h"

namespace fs = std::filesystem;

SERVICE_STATUS ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE hStatus = NULL;
bool stopService = false;


std::wstring StringToWString(const std::string& str) {
    if (str.empty()) {
        return std::wstring();  // Return an empty wstring if the input string is empty
    }

    // Get the size needed for the wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    if (size_needed == 0) {
        throw std::runtime_error("Error converting string to wide string: invalid input or encoding.");
    }

    std::wstring wstrTo(size_needed, 0);  // Allocate space for the wide string

    // Perform the conversion from UTF-8 string to wide string
    int result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    if (result == 0) {
        throw std::runtime_error("Error during string conversion.");
    }

    return wstrTo;
}


void ServiceControlHandler(DWORD request) {
    switch (request) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Set the service status to pending stop
        ServiceStatus.dwWin32ExitCode = 0;
        ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(hStatus, &ServiceStatus);

        stopService = true;  // Signal to stop the service

        // Once the service has stopped, update the status
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &ServiceStatus);
        return;

    default:
        // For unknown control commands, simply update the service status
        SetServiceStatus(hStatus, &ServiceStatus);
        break;
    }
}


void ServiceMain(DWORD argc, LPTSTR* argv) {
    spdlog::info("[ServiceMain] Initializing NexusManager...");

    // Initialize the service status structure
    ServiceStatus.dwServiceType = SERVICE_WIN32;
    ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    // Register the control handler function for the service
    hStatus = RegisterServiceCtrlHandler(L"DCSStreamingNexusManager", (LPHANDLER_FUNCTION)ServiceControlHandler);

    // Check if the registration of the control handler function was successful
    if (hStatus == NULL) {
        spdlog::error("[ServiceMain] Failed to register service control handler. Error: {}", GetLastError());

        ServiceStatus.dwWin32ExitCode = GetLastError();
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &ServiceStatus);
        return;
    }

    try {
        
        
        // Step 1: Initialize the main service class
        MainService nexusManager;

        // Step 2: Load configuration from a file (command-line arguments are NOT used)
        if (!nexusManager.LoadConfiguration()) {
            spdlog::error("[ServiceMain] Failed to load configuration.");
            ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(hStatus, &ServiceStatus);
            return;
        }

        // Step 3: Set the service to RUNNING state
        ServiceStatus.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus(hStatus, &ServiceStatus);
        spdlog::info("[ServiceMain] Service is now running.");

        // Step 4: Start the main service logic
        nexusManager.StartNexusManager();

        // Step 5: Main loop - Wait for the stop signal
        while (!stopService) {
            Sleep(1000); // Prevents CPU overuse by sleeping for 1 second per iteration
        }

        // Step 6: When a stop signal is received, stop the service
        spdlog::info("[ServiceMain] Stopping NexusManager...");
        nexusManager.StopNexusManager();
    }
    catch (const std::exception& e) {
        spdlog::error("Exception occurred in service: {}", e.what());


        // Set the service status to stopped with an error code
        ServiceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(hStatus, &ServiceStatus);
        return;
    }

    // Finally, set the service status to stopped
    ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(hStatus, &ServiceStatus);
    spdlog::info("[ServiceMain] Service stopped.");

}


// Function to configure automatic service restart on failure
void ConfigureServiceRecovery(SC_HANDLE scService) {
    // Check if the provided service handle is valid
    if (scService == NULL) {
        spdlog::error("Invalid service handle provided for recovery configuration.");
        return;
    }

    // Define recovery actions
    SC_ACTION actions[3];

    // The first action is to restart the service after 1 minute
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 60000;  // 60 seconds

    // The second action is to restart the service after 5 minutes if the first attempt fails
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 300000;  // 300 seconds

    // The third action is to stop further recovery attempts after two failures
    actions[2].Type = SC_ACTION_NONE;  // No further action
    actions[2].Delay = 0;

    SERVICE_FAILURE_ACTIONS failureActions;
    failureActions.dwResetPeriod = 86400;  // Reset the failure count after 1 day
    failureActions.lpRebootMsg = NULL;     // No reboot message
    failureActions.lpCommand = NULL;       // No specific command to run on failure
    failureActions.cActions = 3;           // Total number of actions
    failureActions.lpsaActions = actions;  // Pointer to the array of actions

    // Attempt to configure service recovery options
    if (!ChangeServiceConfig2(scService, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions)) {
        spdlog::error("Failed to configure service recovery options. Error code: {}", GetLastError());
    }
    else {
        spdlog::info("Service recovery options configured: service will restart on failure.");
    }
}



void InstallService(CommandLineParser& parser) {
    std::wstring serviceName = L"DCSStreamingNexusManager";

    // Check if the service is already installed
    if (MainService::IsServiceInstalled(serviceName)) {
        spdlog::error("Service is already installed.");
        return;
    }

    // Open the Service Control Manager to create the service
    SC_HANDLE scManager = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scManager == NULL) {
        spdlog::error("Failed to open Service Control Manager. Error code: {}", GetLastError());
        return;
    }
    UpgradePathManager path;

    // Get the installation path for the executable file
    std::string installPath = path.GetUpgradeDirectory();  // Dynamically select the directory based on system architecture

    // Copy the executable file to the installation path
    if (!MainService::CopyExeToInstallPath(installPath)) {
        spdlog::error("Failed to copy exe to the install directory.");
        CloseServiceHandle(scManager);  // Close the Service Control Manager handle
        return;
    }

    std::wstring exePath = L"\"" + StringToWString(installPath) + L"\\ServiceUpdater.exe\"";


    // Create the service 
    SC_HANDLE scService = CreateService(
        scManager,
        serviceName.c_str(),                         // Service name
        L"DCS Streaming Nexus Manager",              // Display name
        SERVICE_ALL_ACCESS,                          // Desired access
        SERVICE_WIN32_OWN_PROCESS,                   // Service type
        SERVICE_AUTO_START,                          // Start type (auto start on boot)
        SERVICE_ERROR_NORMAL,                        // Error control
        exePath.c_str(),                             // Binary path
        NULL, NULL, NULL, NULL, NULL                 // Dependencies and credentials
    );

    if (scService == NULL) {
        spdlog::error("Failed to create service. Error code: {}", GetLastError());
        CloseServiceHandle(scManager);
        return;
    }

    spdlog::info("Service installed successfully.");

    // Configure automatic service restart in case of failure
    ConfigureServiceRecovery(scService);

    // Create an instance of ServiceConfig to retrieve the configuration path
    std::string configFilePath = path.GetMainConfig();

    // Save the configuration to a JSON file using the parser
    if (parser.saveConfigToFile(configFilePath)) {
        spdlog::info("Configuration saved successfully at: {}", configFilePath);
    }
    else {
        spdlog::error("Failed to save configuration at: {}", configFilePath);
    }

    // Automatically start the service
    if (!StartService(scService, 0, NULL)) {
        spdlog::error("Failed to start service. Error code: {}", GetLastError());
    }
    else {
        spdlog::info("Service started successfully.");
    }


    CloseServiceHandle(scService);
    CloseServiceHandle(scManager);
}



void UninstallMainService() {
    // Define the service names
    UpgradePathManager path;
    std::wstring mainServiceName = L"DCSStreamingNexusManager";  // Main service
    
    // Uninstall the main service
    spdlog::info("Uninstalling main service: DCS Streaming Nexus Manager...");
    MainService::UninstallServiceSafe(mainServiceName);

    MainService::RemoveDirectoryContents(path.GetUpgradeDirectory());  // Remove the contents of the "CSM2.0" directory
}
void FullUninstallService() {
    // Define the service names
    UpgradePathManager path;
    std::wstring mainServiceName = L"DCSStreamingNexusManager";  // Main service
    std::wstring firstServiceName = path.GetService1Name();          // Second service
    std::wstring secondServiceName = path.GetService3Name();          // Second service
    std::wstring watchdogServiceName = path.GetService2Name(); // Watchdog service

    // Uninstall the main service
    spdlog::info("Uninstalling main service: DCS Streaming Nexus Manager...");
    MainService::UninstallServiceSafe(mainServiceName);

    spdlog::info("Uninstalling first service...");
    MainService::UninstallServiceSafe(firstServiceName);

    // Uninstall the second service
    spdlog::info("Uninstalling second service...");
    MainService::UninstallServiceSafe(secondServiceName);

    // Uninstall the watchdog service
    spdlog::info("Uninstalling watchdog service...");
    MainService::UninstallServiceSafe(watchdogServiceName);

    MainService::RemoveDirectoryContents(path.GetRootDir());  // Remove the contents of the "CSM2.0" directory
}


void PrintBanner() {
    std::cout << R"(
     '##::: ##:'########:'##::::'##:'##::::'##::'######::::'##::::'##::::'###::::'##::: ##::::'###:::::'######:::'########:'########::
     ###:: ##: ##.....::. ##::'##:: ##:::: ##:'##... ##:    ###::'###:::'## ##::: ###:: ##:::'## ##:::'##... ##:: ##.....:: ##.... ##:
     ####: ##: ##::::::::. ##'##::: ##:::: ##: ##:::..::    ####'####::'##:. ##:: ####: ##::'##:. ##:: ##:::..::: ##::::::: ##:::: ##:
     ## ## ##: ######:::::. ###:::: ##:::: ##:. ######::    ## ### ##:'##:::. ##: ## ## ##:'##:::. ##: ##::'####: ######::: ########::
     ##. ####: ##...:::::: ## ##::: ##:::: ##::..... ##:    ##. #: ##: #########: ##. ####: #########: ##::: ##:: ##...:::: ##.. ##:::
     ##:. ###: ##:::::::: ##:. ##:: ##:::: ##:'##::: ##:    ##:.:: ##: ##.... ##: ##:. ###: ##.... ##: ##::: ##:: ##::::::: ##::. ##::
     ##::. ##: ########: ##:::. ##:. #######::. ######::    ##:::: ##: ##:::: ##: ##::. ##: ##:::: ##:. ######::: ########: ##:::. ##:
     ..::::..::........::..:::::..:::.......::::......:::..:::::..::..:::::..::..::::..::..:::::..:::......::::........::..:::::..::  
    )" << std::endl;

    spdlog::info("DCS STREAMING NEXUS MANAGER v1.0.0.1 - Service Started");
    spdlog::info("Build Date: {}", __DATE__);
    spdlog::info("Waiting for tasks...");
}

int main(int argc, char* argv[]) {
    
    
    try {
        PrintBanner();
        // Check if command-line arguments are provided
        if (argc > 1) {
            std::string arg = argv[1];

            // Check if the first argument is "install"
            if (arg == "install") {
                // Verify that all required parameters are present
                if (argc < 5) {
                    spdlog::error("Missing required parameters for installation.");
                    spdlog::info("Usage: ServiceUpdater.exe install --companyid <id> --region <region> --siteid <id>");
                    return 1;
                }

                // Parse command-line arguments (skip the first "install" argument)
                CommandLineParser parser(argc - 1, &argv[1]);

                // If command-line parsing fails, log an error
                if (!parser.parse()) {
                    spdlog::error("Failed to parse command line arguments.");
                    return 1;
                }

                // Proceed with service installation using parsed arguments
                InstallService(parser);

                /*UpgradePathManager path;

                std::string configFilePath = path.GetMainConfig();

                // Save the configuration to a JSON file using the parser
                if (parser.saveConfigToFile(configFilePath)) {
                    spdlog::info("Configuration saved successfully at: {}", configFilePath);
                }
                else {
                    spdlog::error("Failed to save configuration at: {}", configFilePath);
                }

                MainService nexusManager;

                if (!nexusManager.LoadConfiguration()) {
                    return 1;
                }
                nexusManager.StartNexusManager();*/
            }
            // Check if the first argument is "uninstall"
            else if (arg == "uninstall") {
                // Uninstall the service
                UninstallMainService();
            }
            else if (arg == "uninstall_all") {
                // Uninstall the service
                FullUninstallService();
            }
            else {
                // Handle unknown command
                spdlog::error("Unknown command '{}'.", arg);
                spdlog::info("Usage: ServiceUpdater.exe install|uninstall|uninstall_all");
                return 1;
            }

            return 0;  // Successful execution
        }

        // If no command-line arguments are provided, start the service
        wchar_t serviceName[] = L"DCSStreamingNexusManager";

        // Define the service table for the service control dispatcher
        SERVICE_TABLE_ENTRY ServiceTable[] = {
            {serviceName, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
            {NULL, NULL}  // This table must end with a NULL entry
        };

        // Attempt to start the service control dispatcher
        if (!StartServiceCtrlDispatcher(ServiceTable)) {
            spdlog::error("Failed to start service control dispatcher. Error code: {}", GetLastError());
            return 1;
        }
    }
    catch (const std::exception& e) {
        // Log any exceptions that occur during execution
        spdlog::error("Error: Exception occurred - {}", e.what());
        return 1;
    }

    return 0;
}

