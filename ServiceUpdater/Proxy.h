#ifndef PROXY_H
#define PROXY_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include "Logger.h"
#include "DecryptionManager.h"


class Proxy {
public:
    explicit Proxy(const std::string& configPath) {
        LOG_INFO("Initializing Proxy class...");
        config = loadConfig(configPath);

        if (config.contains("proxy") && config["proxy"].value("enabled", false)) {
            proxy_enabled = true;
            encrypted = config["proxy"].value("encrypted", false);
            bypass_list = config["proxy"].value("bypass", std::vector<std::string>{});
            ssl_enabled = config["proxy"].contains("ssl") && config["proxy"]["ssl"].value("enabled", false);

            LOG_INFO("Proxy is ENABLED. Configuration loaded from {}", configPath);

        }
        else {
            proxy_enabled = false;
            LOG_WARN("Proxy is DISABLED in the configuration.");

        }
    }

    bool proxyDownload(const std::string& url, const std::string& destinationPath) {
        LOG_INFO("Starting file download from: {}", url);


        try {
            if (!makeCurlRequestWithProxy(url, destinationPath)) {
                LOG_ERROR("Failed to download file from URL: {}", url);

                return false;
            }

            LOG_INFO("File successfully downloaded to: {}", destinationPath);

            return true;

        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception occurred in proxyDownload: {}", e.what());

            return false;
        }
        catch (...) {
            LOG_ERROR("Unknown exception occurred in proxyDownload.");
            return false;
        }
    }

    /**
     * @brief Checks if the proxy is enabled.
     *
     * This function returns whether the proxy is currently enabled in the configuration.
     *
     * @return true if the proxy is enabled, false otherwise.
     */
    bool isProxyEnabled() const {
        return proxy_enabled;
    }

    /**
     * @brief Makes a CURL request using a configured proxy.
     *
     * This function sends an HTTP request to the given URL, saving the response to the specified
     * output file. It first checks if the proxy is enabled and if the request should bypass the proxy.
     * If the proxy is required, it retrieves the proxy settings from the configuration and constructs
     * the proxy URL before making the request.
     *
     * @param url The URL to send the request to.
     * @param output_file The path where the response should be saved.
     * @return true if the request was successful, false otherwise.
     */
    bool makeCurlRequestWithProxy(const std::string& url, const std::string& output_file) {
        LOG_INFO("Preparing to make a request to: {}", url);


        if (!proxy_enabled) {
            LOG_ERROR("Proxy is not enabled. Cannot proceed with proxy request.");
            return false;
        }

        if (isBypassed(url)) {
            LOG_INFO("Bypassing proxy for URL: {}", url);

            return makeCurlRequest(url, "", output_file);
        }


        if (!config.contains("proxy") || !config["proxy"].contains("server")) {
            LOG_ERROR("Proxy configuration is missing in JSON.");

            return false;
        }

        std::string proxy_type = config["proxy"].value("type", "http");
        std::string proxy_host = config["proxy"]["server"].value("host", "");
        int proxy_port = config["proxy"]["server"].value("port", 0);

        if (proxy_host.empty() || proxy_port == 0) {
            LOG_ERROR("Invalid proxy host or port configuration.");

            return false;
        }

        std::string proxy_url = proxy_type + "://" + proxy_host + ":" + std::to_string(proxy_port);
        LOG_INFO("Using Proxy: {}", proxy_url);


        return makeCurlRequest(url, proxy_url, output_file);
    }

private:
    nlohmann::json config;
    bool proxy_enabled{ false };
    bool encrypted{ false };
    bool ssl_enabled{ false };
    std::vector<std::string> bypass_list;

    /**
     * @brief Callback function for writing data to a file during a CURL request.
     *
     * This function is used as a write callback by `libcurl` to save the received data into a file.
     * It ensures valid pointers are provided before writing. If any issue arises (e.g., null pointer
     * or file not open), it logs the error and returns `0` to indicate failure.
     *
     * @param contents Pointer to the downloaded data.
     * @param size Size of a single data unit.
     * @param nmemb Number of data units.
     * @param userp Pointer to the file stream where data should be written.
     * @return The number of bytes successfully written, or `0` on failure.
     */
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        if (!contents || !userp) {
            LOG_ERROR("WriteCallback: Invalid pointer passed.");

            return 0;
        }

        std::ofstream* file = static_cast<std::ofstream*>(userp);
        if (!file->is_open()) {
            LOG_ERROR("WriteCallback: File is not open for writing.");

            return 0;
        }
        file->write(static_cast<const char*>(contents), size * nmemb);
        return size * nmemb;
    }
    /**
     * @brief Sends an HTTP request using `libcurl` with optional proxy and SSL settings.
     *
     * This function makes an HTTP request to the given URL and saves the response to an output file.
     * It supports proxy settings (if provided), SSL verification options, and custom HTTP headers.
     * If a proxy is configured with authentication, it handles the necessary credentials securely.
     *
     * The function logs all key details, including whether SSL verification is enabled, proxy details,
     * and potential errors encountered during the request.
     *
     * @param url The URL to request.
     * @param proxy The proxy server address, or an empty string if no proxy is used.
     * @param output_file The path where the response should be saved.
     * @return true if the request was successful and the file was downloaded, false otherwise.
     */
    bool makeCurlRequest(const std::string& url, const std::string& proxy, const std::string& output_file) {
        CURL* curl = curl_easy_init(); 
        CURLcode res;
        long response_code = 0;
        struct curl_slist* headers = nullptr;

        if (!curl) {
            LOG_ERROR("Failed to initialize CURL.");
            return false;
        }

        try {
            std::ofstream file(output_file, std::ios::binary);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open file for writing: {}", output_file);
                throw std::runtime_error("Failed to open output file");
            }

            LOG_INFO("Downloading file from: {}", url);

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

            // Proxy settings
            if (!proxy.empty()) {
                LOG_INFO("Using Proxy: {}", proxy);
                curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());

                if (config["proxy"]["type"].get<std::string>() == "https") {
                    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);
                }

                if (config["proxy"]["authentication"]["enabled"].get<bool>()) {
                    std::string username = config["proxy"]["authentication"]["username"].get<std::string>();
                    std::string password = config["proxy"]["authentication"]["password"].get<std::string>();

                    if (encrypted) {
                        password = decryptPassword(password);
                    }

                    std::string proxy_auth = username + ":" + password;
                    curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxy_auth.c_str());

                    LOG_INFO("Using Proxy Authentication for user: {}", username);
                }
            }

            // SSL Settings
            if (ssl_enabled) {
                bool verify_peer = config["proxy"]["ssl"]["verify_peer"].get<bool>();
                bool verify_host = config["proxy"]["ssl"]["verify_host"].get<bool>();
                std::string ca_cert = config["proxy"]["ssl"]["ca_cert_path"].get<std::string>();
                std::string client_cert = config["proxy"]["ssl"]["client_cert_path"].get<std::string>();
                std::string client_key = config["proxy"]["ssl"]["client_key_path"].get<std::string>();

                LOG_INFO("SSL/TLS Settings:");
                LOG_INFO(" - Verify Peer: {}", verify_peer);
                LOG_INFO(" - Verify Host: {}", verify_host);
                LOG_INFO(" - CA Cert Path: {}", ca_cert.empty() ? "Not Provided" : ca_cert);
                LOG_INFO(" - Client Cert Path: {}", client_cert.empty() ? "Not Provided" : client_cert);
                LOG_INFO(" - Client Key Path: {}", client_key.empty() ? "Not Provided" : client_key);

                curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYPEER, verify_peer ? 1L : 0L);
                curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYHOST, verify_host ? 2L : 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_peer ? 1L : 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_host ? 2L : 0L);

                if (!ca_cert.empty()) {
                    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert.c_str());
                }
                if (!client_cert.empty()) {
                    curl_easy_setopt(curl, CURLOPT_SSLCERT, client_cert.c_str());
                }
                if (!client_key.empty()) {
                    curl_easy_setopt(curl, CURLOPT_SSLKEY, client_key.c_str());
                }
            }
            else {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            // HTTP Headers
            headers = curl_slist_append(headers, "Accept: */*");
            headers = curl_slist_append(headers, "User-Agent: curl/8.11.0");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);


            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            if (res != CURLE_OK) {
                throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
            }

            if (response_code != 200) {
                throw std::runtime_error("HTTP request failed with status: " + std::to_string(response_code));
            }

            LOG_INFO("File successfully downloaded: {}", output_file);

            return true;
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in makeCurlRequest: {}", e.what());
        }
        catch (...) {
            LOG_ERROR("Unknown exception occurred in makeCurlRequest.");
        }

        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(curl);

        return false;
    }

    /**
     * @brief Loads a JSON configuration file from the specified path.
     *
     * This function attempts to read and parse a JSON file from the given `filepath`.
     * If the file cannot be opened or the JSON parsing fails, it logs the error and returns an empty JSON object.
     *
     * @param filepath The path to the configuration file.
     * @return A `nlohmann::json` object containing the parsed configuration, or an empty object on failure.
     */
    nlohmann::json loadConfig(const std::string& filepath) {
        LOG_INFO("Loading configuration file: {}", filepath);

        std::ifstream file(filepath);
        nlohmann::json config;
        if (!file.is_open()) {
            LOG_ERROR("Error: Could not open configuration file: {}", filepath);

            return {};
        }
        try {
            file >> config;
            LOG_INFO("Successfully parsed JSON configuration from {}", filepath);

        }
        catch (const std::exception& e) {
            LOG_ERROR("Error: Failed to parse JSON file: {}", e.what());

            return {};
        }
        return config;
    }

    /**
     * @brief Checks if a given URL should bypass the proxy.
     *
     * This function compares the given URL against a list of hosts that should bypass
     * the proxy. If the URL contains a host from the bypass list, it returns `true`,
     * indicating that the request should be made directly without using a proxy.
     *
     * @param url The URL to check.
     * @return true if the URL should bypass the proxy, false otherwise.
     */
    bool isBypassed(const std::string& url) {
        for (const auto& host : bypass_list) {
            if (url.find(host) != std::string::npos) {
                LOG_INFO("Bypassing proxy for host: {}", host);

                return true;
            }
        }
        return false;
    }

    /**
     * @brief Decrypts an encrypted password.
     *
     * This function utilizes `DecryptionManager` to decrypt the provided encrypted password.
     * If decryption fails, it throws an exception. It logs a warning indicating that credentials are being decrypted.
     *
     * @param encrypted_password The encrypted password to decrypt.
     * @return The decrypted password as a string.
     * @throws std::runtime_error if decryption fails.
     */
    std::string decryptPassword(const std::string& encrypted_password) {
        LOG_WARN("Decrypting Credentials.");
        DecryptionManager decryptor;
        std::string decryptedPassword = decryptor.decrypt_field(encrypted_password);

        // Check if the decryption succeeded
        if (decryptedPassword.empty()) {
            throw std::runtime_error("Failed to decrypt Proxy Password.");
        }

        return decryptedPassword;

    }
};

#endif // PROXY_H
