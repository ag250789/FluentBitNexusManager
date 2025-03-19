# DCS Streaming Nexus Manager Documentation

## Introduction

**DCS Streaming Nexus Manager** is a service responsible for managing two other services:

- **DCS Streaming Agent Controller**
- **DCS Streaming Agent Watchdog**

The Nexus Manager ensures that these services are installed, updated, and maintained based on predefined configuration rules. It retrieves installation and upgrade packages from an **Azure Storage** repository, dynamically forming the download URL based on input parameters such as **Company ID, Site ID, and Region**.

## Initial Installation

During its first installation, the **Nexus Manager** downloads the initial installation package from Azure Storage:

📦 **Package:** `ncrv_dcs_streaming_service_upgrade_manager.zip`

**Possible Contents of the Update Package**

The update package may contain the following files:

📂 **FluentBitManager.exe** - Main executable responsible for managing **Fluent Bit** configurations.

📂 **WatchdogFluentBit.exe** - Watchdog process that monitors and ensures **Fluent Bit Controller** remains operational.

📂 **install_config.json** *(optional)* - Configuration file used during the initial installation process to define installation behavior.

📂 **upgrade_config.json** *(optional)* - Configuration file that defines upgrade policies, such as full reinstall requirements or just update-restart of service.

**Note:** The configuration files are optional. If not provided, the system will apply default settings based on internal logic.

🚀 This structure ensures flexibility in updates while maintaining a seamless deployment process.



The installation process follows these steps:

1. **Check if services are already installed:**
   - If not installed, it extracts the package and installs **DCS Streaming Agent Controller** and **DCS Streaming Agent Watchdog**.
   - If installed, it reads the `install_config.json` file from the zip package and determines the required action.

### Sample `install_config.json` format:


```json
{
    "enable_initial_install": true,
    "install_reason": "New deployment for site 1234",
    "required_version": "2.5.1",
    "timestamp": "2025-02-22T10:30:00Z",
    "services": [
        {
            "name": "DCSStreamingAgentController",
            "exe": "FluentBitManager.exe"
        },
        {
            "name": "DCSStreamingAgentWatchdog",
            "exe": "WatchdogFluentBit.exe"
        }
    ]
}
```

- If **`enable_initial_install`** is `true`, the Nexus Manager ensures both services are installed.
- If a **reinstallation** is required, it reinstalls the services according to the provided `required_version`.
- Logs the installation reason and timestamp for tracking purposes.

## Periodic Updates

Every day at **1 AM**, the Nexus Manager checks for updates by downloading a new zip package from Azure Storage. Instead of relying on version numbers, it verifies the integrity of the files by **comparing their hash values**. If the files differ, the services are updated accordingly.

### Sample `upgrade_config.json` format:

```json
{
    "full_reinstall": false,
    "reason": "Critical security update",
    "required_version": "2.3.0",
    "timestamp": "2025-02-21T14:00:00Z"
}
```

🔹 **How updates are handled:**
- **File Integrity Check:** The system downloads the update package and calculates the hash of each file.
- If the **hash differs from the installed version**, the service updates the respective components.
- If `full_reinstall` is `true`, the services are completely removed and reinstalled.
- Logs the update reason and timestamp for tracking purposes.


## Overview

The **DCS Streaming Nexus Manager** is designed to maintain service integrity through a structured and modular approach. It ensures configuration files remain unchanged unless an intentional update is applied. The system relies on a **hash-based verification mechanism**, preventing unnecessary updates while ensuring stability and security.



## **Hash-Based Update Mechanism**
Instead of relying on version numbers, **DCS Streaming Nexus Manager** employs a **hash-based approach** to detect configuration changes, ensuring:

🔹 Reliable detection of modifications, even if version numbers remain unchanged.  
🔹 Prevention of redundant updates by applying changes only when necessary.  
🔹 Secure integrity verification using SHA-256 cryptographic hashes.  


The **DCS Streaming Nexus Manager** is designed to manage and monitor critical services by securely downloading, verifying, and updating configuration files. The system ensures data integrity through a hash-based verification mechanism while leveraging secure downloads, proxy support, and encryption for added security.


**Logger Configuration (Optional)**

The `log_config` parameter allows specifying a JSON configuration file to define logging settings. However, **it is recommended to omit this parameter**, as the logger will initialize with default values if not provided.

### Example of a `LoggerConfig.json` file:
```json
{
    "log_level": "info",
    "log_directory": "C:/Temp/logs_test",
    "log_file": "test.log",
    "max_file_size": 4092,
    "max_files": 5,
    "async_logging": true,
    "delete_logs_older_than_days": 1
}
```
If the configuration file is missing or invalid, the system will use default values to ensure uninterrupted logging.

---

### **CronTab Expression for Scheduling**

The `crontab` parameter defines the execution schedule for updates. The format follows the standard cron expression:

```
Seconds Minutes Hours Day-of-Month Month Day-of-Week
```

#### **Examples:**
1. **Every 5 minutes:**
   ```cron
   0 */5 * * * ?
   ```
   **Explanation:**
   - **Seconds:** `0` (executes at the start of the minute)
   - **Minutes:** `*/5` (runs every 5 minutes)
   - **Hours:** `*` (every hour)
   - **Day of Month:** `*` (every day)
   - **Month:** `*` (every month)
   - **Day of Week:** `?` (ignored, as the day-of-month is specified)

2. **At exactly 7:00 PM (19:00):**
   ```cron
   0 0 19 * * ?
   ```
   **Explanation:**
   - **Seconds:** `0`
   - **Minutes:** `0`
   - **Hours:** `19` (7 PM)
   - **Day of Month:** `*` (every day)
   - **Month:** `*` (every month)
   - **Day of Week:** `?`

3. **At exactly 3:00 PM (15:00):**
   ```cron
   0 0 15 * * ?
   ```
   **Explanation:**
   - **Seconds:** `0`
   - **Minutes:** `0`
   - **Hours:** `15` (3 PM)
   - **Day of Month:** `*` (every day)
   - **Month:** `*` (every month)
   - **Day of Week:** `?`

#### **Testing Recommendation**
For testing purposes, it is advised to set a crontab expression that triggers execution **every few minutes**. This allows for quick validation of scheduled updates and service behavior without long wait times.
For more details and real-time crontab scheduling validation, refer to [Crontab Guru](https://crontab.guru/#*/3_*_*_*_*)


### **Component Interaction**

🔄 **Receives command-line arguments** ➝ Validates inputs ➝ Stores data in JSON format ➝ Retrieves configuration when needed.

📌 If errors are detected, the process stops, preventing incorrect configurations.

📖 This structured approach ensures robust command-line parsing, minimizes user errors, and provides a reliable foundation for Nexus Manager configuration.




**### Installation and Uninstallation Guide**

This section provides detailed instructions on how to install and uninstall the **ServiceUpdater** application and the services it manages. 

---

### **Installation Commands**

To install **ServiceUpdater**, you need to provide mandatory parameters like **Company ID, Region, and Site ID**. Additionally, optional parameters such as **log configuration, proxy configuration, and crontab scheduling** can be included.

#### **Basic Installation Command:**
```sh
ServiceUpdater.exe install --companyid dcs01 --region prep --siteid 20251627 --crontab "0 */3 * * * ?"
```
🔹 **Parameters Explanation:**
- `--companyid dcs01` → Specifies the company identifier.
- `--region prep` → Defines the deployment region.
- `--siteid 20251627` → Identifies the installation site.
- `--crontab "0 */3 * * * ?"` → Configures periodic updates every **3 minutes**.

#### **Installation with Custom Logger and Proxy Configuration:**
```sh
ServiceUpdater.exe install --companyid dcs01 --region prep --siteid 20251627 --log_config "C:\Temp\loggerConfig.json" --proxy_config "C:\Temp\proxyConfigHTTP.json" --crontab "0 */7 * * * ?"
```
🔹 **Additional Parameters:**
- `--log_config "C:\Temp\loggerConfig.json"` → Specifies a custom logging configuration file.
- `--proxy_config "C:\Temp\proxyConfigHTTP.json"` → Provides a proxy configuration file if network restrictions apply.
- `--crontab "0 */7 * * * ?"` → Sets the update interval to **every 7 minutes**.

⚠️ **Note:** It is recommended **not to specify the log configuration file** unless necessary for testing, as ServiceUpdater initializes with **default logging settings**.

---

### **Uninstallation Commands**

To remove the **ServiceUpdater** or all managed services, use the following commands:

#### **Uninstall Only ServiceUpdater:**
```sh
ServiceUpdater.exe uninstall
```
🔹 This will **remove only the ServiceUpdater application**, leaving the managed services untouched.

#### **Uninstall ServiceUpdater and All Managed Services:**
```sh
ServiceUpdater.exe uninstall_all
```
🔹 This will **remove ServiceUpdater and all services it has installed and managed**.

---



**By following these commands, you can efficiently install, configure, and remove ServiceUpdater and its related services.** 🚀
