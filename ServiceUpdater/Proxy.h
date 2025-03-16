#ifndef PROXY_H
#define PROXY_H

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <curl_openssl/curl.h>
#include <spdlog/spdlog.h>
#include "Logger.h"
#include "DecryptionManager.h"


class Proxy {
public:
    explicit Proxy(const std::string& configPath) {
        //spdlog::info("Initializing Proxy class...");
        LOG_INFO("Initializing Proxy class...");
        config = loadConfig(configPath);

        if (config.contains("proxy") && config["proxy"].value("enabled", false)) {
            proxy_enabled = true;
            encrypted = config["proxy"].value("encrypted", false);
            bypass_list = config["proxy"].value("bypass", std::vector<std::string>{});
            ssl_enabled = config["proxy"].contains("ssl") && config["proxy"]["ssl"].value("enabled", false);

            //spdlog::info("Proxy is ENABLED. Configuration loaded from {}", configPath);
            LOG_INFO("Proxy is ENABLED. Configuration loaded from {}", configPath);

        }
        else {
            proxy_enabled = false;
            //spdlog::warn("Proxy is DISABLED in the configuration.");
            LOG_WARN("Proxy is DISABLED in the configuration.");

        }
    }

    bool proxyDownload(const std::string& url, const std::string& destinationPath) {
        //spdlog::info("Starting file download from: {}", url);
        LOG_INFO("Starting file download from: {}", url);


        try {
            if (!makeCurlRequestWithProxy(url, destinationPath)) {
                //spdlog::error("Failed to download file from URL: {}", url);
                LOG_ERROR("Failed to download file from URL: {}", url);

                return false;
            }

            //spdlog::info("File successfully downloaded to: {}", destinationPath);
            LOG_INFO("File successfully downloaded to: {}", destinationPath);

            return true;

        }
        catch (const std::exception& e) {
            //spdlog::error("Exception occurred in proxyDownload: {}", e.what());
            LOG_ERROR("Exception occurred in proxyDownload: {}", e.what());

            return false;
        }
        catch (...) {
            //spdlog::error("Unknown exception occurred in proxyDownload.");
            LOG_ERROR("Unknown exception occurred in proxyDownload.");

            return false;
        }
    }


    bool isProxyEnabled() const {
        return proxy_enabled;
    }

    bool makeCurlRequestWithProxy(const std::string& url, const std::string& output_file) {
        //spdlog::info("Preparing to make a request to: {}", url);
        LOG_INFO("Preparing to make a request to: {}", url);


        if (!proxy_enabled) {
            //spdlog::error("Proxy is not enabled. Cannot proceed with proxy request.");
            LOG_ERROR("Proxy is not enabled. Cannot proceed with proxy request.");
            return false;
        }

        // Provera da li URL treba da se zaobi?e u proxy podešavanjima
        if (isBypassed(url)) {
            //spdlog::info("Bypassing proxy for URL: {}", url);
            LOG_INFO("Bypassing proxy for URL: {}", url);

            return makeCurlRequest(url, "", output_file);
        }


        if (!config.contains("proxy") || !config["proxy"].contains("server")) {
            //spdlog::error("Proxy configuration is missing in JSON.");
            LOG_ERROR("Proxy configuration is missing in JSON.");

            return false;
        }

        std::string proxy_type = config["proxy"].value("type", "http");
        std::string proxy_host = config["proxy"]["server"].value("host", "");
        int proxy_port = config["proxy"]["server"].value("port", 0);

        if (proxy_host.empty() || proxy_port == 0) {
            //spdlog::error("Invalid proxy host or port configuration.");
            LOG_ERROR("Invalid proxy host or port configuration.");

            return false;
        }

        std::string proxy_url = proxy_type + "://" + proxy_host + ":" + std::to_string(proxy_port);
        //spdlog::info("Using Proxy: {}", proxy_url);
        LOG_INFO("Using Proxy: {}", proxy_url);


        return makeCurlRequest(url, proxy_url, output_file);
    }

private:
    nlohmann::json config;
    bool proxy_enabled{ false };
    bool encrypted{ false };
    bool ssl_enabled{ false };
    std::vector<std::string> bypass_list;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        if (!contents || !userp) {
            //spdlog::error("WriteCallback: Invalid pointer passed.");
            LOG_ERROR("WriteCallback: Invalid pointer passed.");

            return 0;
        }

        std::ofstream* file = static_cast<std::ofstream*>(userp);
        if (!file->is_open()) {
            //spdlog::error("WriteCallback: File is not open for writing.");
            LOG_ERROR("WriteCallback: File is not open for writing.");

            return 0;
        }
        file->write(static_cast<const char*>(contents), size * nmemb);
        return size * nmemb;
    }

    bool makeCurlRequest(const std::string& url, const std::string& proxy, const std::string& output_file) {
        CURL* curl = curl_easy_init(); // Inicijalizujemo CURL odmah
        CURLcode res;
        long response_code = 0;
        struct curl_slist* headers = nullptr;

        if (!curl) {
            //spdlog::error("Failed to initialize CURL.");
            LOG_ERROR("Failed to initialize CURL.");
            return false;
        }

        try {
            std::ofstream file(output_file, std::ios::binary);
            if (!file.is_open()) {
                //spdlog::error("Failed to open file for writing: {}", output_file);
                LOG_ERROR("Failed to open file for writing: {}", output_file);
                throw std::runtime_error("Failed to open output file");
            }

            //spdlog::info("Downloading file from: {}", url);
            LOG_INFO("Downloading file from: {}", url);

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

            // Proxy settings
            if (!proxy.empty()) {
                //spdlog::info("Using Proxy: {}", proxy);
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

                    //spdlog::info("Using Proxy Authentication for user: {}", username);
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

                /*spdlog::info("SSL/TLS Settings:");
                spdlog::info(" - Verify Peer: {}", verify_peer);
                spdlog::info(" - Verify Host: {}", verify_host);
                spdlog::info(" - CA Cert Path: {}", ca_cert.empty() ? "Not Provided" : ca_cert);
                spdlog::info(" - Client Cert Path: {}", client_cert.empty() ? "Not Provided" : client_cert);
                spdlog::info(" - Client Key Path: {}", client_key.empty() ? "Not Provided" : client_key);*/

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

            // Izvršavanje CURL zahteva
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            if (res != CURLE_OK) {
                throw std::runtime_error(std::string("CURL request failed: ") + curl_easy_strerror(res));
            }

            if (response_code != 200) {
                throw std::runtime_error("HTTP request failed with status: " + std::to_string(response_code));
            }

            //spdlog::info("File successfully downloaded: {}", output_file);
            LOG_INFO("File successfully downloaded: {}", output_file);

            return true;
        }
        catch (const std::exception& e) {
            //spdlog::error("Exception in makeCurlRequest: {}", e.what());
            LOG_ERROR("Exception in makeCurlRequest: {}", e.what());
        }
        catch (...) {
            //spdlog::error("Unknown exception occurred in makeCurlRequest.");
            LOG_ERROR("Unknown exception occurred in makeCurlRequest.");
        }

        // Sigurno osloba?anje resursa u slu?aju greške
        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(curl);

        return false;
    }

    nlohmann::json loadConfig(const std::string& filepath) {
        //spdlog::info("Loading configuration file: {}", filepath);
        LOG_INFO("Loading configuration file: {}", filepath);

        std::ifstream file(filepath);
        nlohmann::json config;
        if (!file.is_open()) {
            //spdlog::error("Error: Could not open configuration file: {}", filepath);
            LOG_ERROR("Error: Could not open configuration file: {}", filepath);

            return {};
        }
        try {
            file >> config;
            //spdlog::info("Successfully parsed JSON configuration from {}", filepath);
            LOG_INFO("Successfully parsed JSON configuration from {}", filepath);

        }
        catch (const std::exception& e) {
            //spdlog::error("Error: Failed to parse JSON file: {}", e.what());
            LOG_ERROR("Error: Failed to parse JSON file: {}", e.what());

            return {};
        }
        return config;
    }

    bool isBypassed(const std::string& url) {
        for (const auto& host : bypass_list) {
            if (url.find(host) != std::string::npos) {
                //spdlog::info("Bypassing proxy for host: {}", host);
                LOG_INFO("Bypassing proxy for host: {}", host);

                return true;
            }
        }
        return false;
    }

    std::string decryptPassword(const std::string& encrypted_password) {
        //spdlog::warn("Decryption function is a placeholder. Implement proper encryption.");
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
