#ifndef URLGENERATOR_H
#define URLGENERATOR_H

#include "DecryptionManager.h"
#include <string>
#include <unordered_map>
#include <cpr/cpr.h>
#include <iostream>

class URLGenerator {
public:

    URLGenerator(const std::string& region, const std::string& customerId, const std::string& siteId, const std::string& blobName)
        : region(region), customerId(customerId), siteId(siteId), blobName(blobName), sasToken(sasToken) {

        // Map of URLs for each region
        regionUrls = {
            {"Prep", "053b8438eb089c795b957a636201b4cb9796977dc826f37cd9deaf728d76c2876832b716576e740aea99791261c82a1f8ce711d0ea04dcf18945abfb8fbe9df926b7cbb0be51a1e000e511407fbfc2058f2c84b6b20acb14a5bd"},
            {"Apac", "8b27e28af3613b9354a96cf9a3964e3d70baf01cd9c2079ea4b27356a0c51732c113ff2636d6a6f21f8a15ac97502d599681961e441457a3f9c95732b3e7bdb6a362a4b7f81b13a048b20be0ab15"},
            {"Europe", "aa113a80dbe7e2c840d8fb924257bdf65f7e61872303d9601c7d176aafbb3a01807aca559355b51d422fde32b1604fdc162fb675d8e1d53b7338773e6e99db253385d916af0ea3162b72a3673d1df8"},
            {"Americas", "32a468c7497909b27f9f7678d4f9c69d68e1c618eb1e9e03f0ae95e8655097a873e80d94ca096e72f9da68e5ca49befe74c6f302a4cbeefecd4e5561f1584e3f9daa0dbf7f6f9f792173c4b08848"},
            {"Proba", "053b8438eb089c795b957a636201b4cb9796977dc826f37cd9deaf728d76c2876832b716576e740aea99791261c82a1f8ce711d0ea04dcf18945abfb8fbe9df926b7cbb0be51a1e000e511407fbfc2058f2c84b6b20acb14a5bd"},

        };

        // Define the map of encrypted SAS tokens for each region inside the constructor
        regionSasTokens = {
            {"Prep", "0d0336a33c2fb552ee1c99bc280ba95c8f51a8d9f486035a867a2101a04ae096d86726ceb69b08e8f45ad64f9a01d5e59652835bc2070cff58dd1f07b2793ec859ab1b4d39ea52befd9a15db2de34841a140cb4b39fb06edba6aaacec951648451320f47d92e3dd4e81b60e0ac48e5fcbb1adc040390e84f9aaf8aaf3db0637ba80ba80ce060f7e40f791b5a47eb4360ee5f8409ac5db67dceb6de6402079e6d18cf8c1aa1bccb60ce1a117144"},
            {"Apac", "63d939dc5bf7cebfd3688f010502b2608690ffa20099a6ab796d7d58b4e8f092a2dc592e1cdc028f7d57a6b24d2d10097fc969d3c63a024590bc2445ba63510c8610eeda0a70bc66cd75df138df7fa142d298bec8c46a4043f7a87540d320d3407825f31a3958d8b4c8f262bf878be7f7908dcc2fc77155196dcef7e10e7058de37eaa1d96f08b3d4fcbf029321bb32c61daf362652c19453085279b21f2ea9e02cb889e9206e8005ab6b9e14c"},
            {"Europe", "f5fe595144b4bce2f11d42bb1f8afad1e3a3c344c657a304bb2cde3e3e2521d8085c0bbc531ec43ee50ae273c209a6312c7758a5db7cd282921a352ed117102065971cabc791900f1a82b71ab24f50a9bd356b6e10754708648307d39127609375739eae66a00806fc18c04641c640e5fd5d5446f7b2a11805d32763073258dd23279c05ac5d4eb1fa346e2c3c800492619e2bc60c56fbc13fd4f73ac9245946f6e3fe3b55e59ca17a61d4c36a"},
            {"Americas", "c3eb7f8c2b2f3da30113beb4a6ffcc893eedda88d8b7a3bfb6721aa02b70ada1ec29077c8dc4335f76b7cea8a55f4a6f3ce413da3eb4004c262b5c0da1d0d5c7eac7ddb2c4a1c583609324c53411f8920249ec1eb943e3f7c5525cc0c73d1100a10921045ca0e9e485dcd11406421a16c132cc413979eefdc4aecee930f9ed7bb3b21bc672a8f8a1fdd5c7013467b20e539884f5b6f20f83d8e0f07f6ba5f8914d0dd37691e3b9ccfd12bad3b2"},
            {"Proba", "0d0336a33c2fb552ee1c99bc280ba95c8f51a8d9f486035a867a2101a04ae096d86726ceb69b08e8f45ad64f9a01d5e59652835bc2070cff58dd1f07b2793ec859ab1b4d39ea52befd9a15db2de34841a140cb4b39fb06edba6aaacec951648451320f47d92e3dd4e81b60e0ac48e5fcbb1adc040390e84f9aaf8aaf3db0637ba80ba80ce060f7e40f791b5a47eb4360ee5f8409ac5db67dceb6de6402079e6d18cf8c1aa1bccb60ce1a117144"},

        };


    }

    /**
     * @brief Decrypts the SAS token for the specified region.
     *
     * @return The decrypted SAS token as a string.
     * @throws std::runtime_error If the encrypted token is not found or decryption fails.
     */
    std::string decryptSasTokenForRegion() const {
        auto it = regionSasTokens.find(region);

        // Check if the token exists and is not empty
        if (it == regionSasTokens.end() || it->second.empty()) {
            throw std::runtime_error("Encrypted SAS token not found or empty for region: " + region);
        }

        DecryptionManager decryptor;
        std::string decryptedToken = decryptor.decrypt_field(it->second);

        // Check if the decryption succeeded
        if (decryptedToken.empty()) {
            throw std::runtime_error("Failed to decrypt SAS token for region: " + region);
        }

        return decryptedToken;
    }

    /**
     * @brief Generates the SAS token for the region by decrypting the stored token.
     *
     * @return The decrypted SAS token. Returns an empty string if decryption fails.
     */
    std::string generateSasToken() const {
        try {
            // Attempt to decrypt and return the SAS token for the region
            return decryptSasTokenForRegion();
        }
        catch (const std::exception& e) {
            // Log or handle the error as needed, returning an empty string on failure
            LOG_ERROR("Exception during SAS token generation: {}", e.what());

            return "";  // Return empty string to indicate failure
        }
    }


    /**
     * @brief Decrypts the base URL for the specified region.
     *
     * @return The decrypted base URL as a string.
     * @throws std::runtime_error If the encrypted base URL is not found or decryption fails.
     */
    std::string decryptBaseUrlForRegion() const {
        auto it = regionUrls.find(region);

        // Check if the base URL exists and is not empty
        if (it == regionUrls.end() || it->second.empty()) {
            throw std::runtime_error("Encrypted base URL not found or empty for region: " + region);
        }

        DecryptionManager decryptor;
        std::string decryptedBaseUrl = decryptor.decrypt_field(it->second);

        // Check if the decryption succeeded
        if (decryptedBaseUrl.empty()) {
            throw std::runtime_error("Failed to decrypt base URL for region: " + region);
        }

        return decryptedBaseUrl;
    }

    /**
     * @brief Generates the base URL for the region by decrypting the stored URL.
     *
     * @return The decrypted base URL. Returns an empty string if decryption fails.
     */
    std::string generateBaseUrl() const {
        try {
            // Attempt to decrypt and return the base URL for the region
            return decryptBaseUrlForRegion();
        }
        catch (const std::exception& e) {
            // Log the exception using spdlog and return an empty string on failure
            LOG_ERROR("Exception during base URL generation: {}", e.what());

            return "";  // Return empty string to indicate failure
        }
    }


    /**
     * @brief Generates a complete URL with the `siteId` parameter included.
     *
     * @return A full URL string including the `siteId`, or an empty string if the region is invalid.
     */
    std::string generateUrlWithSiteId() const {
        // Check if the region is valid
        auto it = regionUrls.find(region);
        if (it == regionUrls.end()) {
            //spdlog::error("Invalid region: {}", region);
            LOG_ERROR("Invalid region: {}", region);

            return "";  // Return an empty string in case of an invalid region
        }

        // Retrieve the decrypted base URL for the region and construct the complete URL
        auto baseUrl = generateBaseUrl();
        return baseUrl + "/" + customerId + "/" + siteId + "/" + blobName + "?" + generateSasToken();
    }

    /**
     * @brief Generates a complete URL without the `siteId` parameter.
     *
     * @return A full URL string excluding the `siteId`, or an empty string if the region is invalid.
     */
    std::string generateUrlWithoutSiteId() const {
        // Check if the region is valid
        auto it = regionUrls.find(region);
        if (it == regionUrls.end()) {
            LOG_ERROR("Invalid region: {}", region);
            return "";  // Return an empty string in case of an invalid region
        }

        // Retrieve the decrypted base URL for the region and construct the complete URL without the siteId
        auto baseUrl = generateBaseUrl();
        return baseUrl + "/" + customerId + "/" + blobName + "?" + generateSasToken();
    }

    /**
     * @brief Checks if a given URL exists by sending an HTTP HEAD request.
     *
     * This function uses `libcurl` to make a HEAD request to the specified URL
     * without downloading the response body. If the request succeeds and returns
     * an HTTP status code of 200, the URL is considered to exist.
     *
     * - Follows redirects if necessary.
     * - Disables SSL certificate and hostname verification (for debugging purposes).
     * - Uses a timeout of 10 seconds for the request.
     *
     * @param url The URL to check.
     * @return true if the URL exists (HTTP 200 response), false otherwise.
     */
    bool urlExists(const std::string& url) const {
        CURL* curl = curl_easy_init();
        if (!curl) {
            LOG_ERROR("Failed to initialize CURL.");

            return false;
        }

        bool exists = false;
        CURLcode res;

        // Configure CURL options for a HEAD request
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // Use HEAD request (no response body)
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // Follow redirects
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Skip SSL certificate validation
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // Skip hostname verification
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);  // Set timeout to 10 seconds

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long response_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            exists = (response_code == 200);
        }
        else {
            std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
        return exists;
    }

    /**
     * @brief Checks if a given URL with `siteId` exists using an HTTP HEAD request.
     *
     * @param url The URL to check for existence.
     * @return True if the URL exists (returns HTTP 200), otherwise false.
     */
    bool checkUrlExists(const std::string& url) const {
        // Send a HEAD request to check if the URL exists
        
        bool exists = urlExists(url);

        if (exists) {
            //spdlog::info("URL exists: {}", url);
            LOG_INFO("URL exists.");

            return true;
        }
        else {
            //spdlog::warn("URL does not exist: {}.", url);
            LOG_WARN("URL does not exists.");

            return false;
        }
    }

    
    

    /**
     * @brief Main method that checks if the URL with `siteId` exists, and if not, checks the URL without `siteId`.
     *
     * @return The valid URL if one exists, or an empty string if neither URL is valid.
     */
    std::string getValidUrl() {
        // Generate URLs with and without `siteId`
        std::string urlWithSiteId = generateUrlWithSiteId();
        std::string urlWithoutSiteId = generateUrlWithoutSiteId();

        //spdlog::info("Generated URL with siteId: {}", urlWithSiteId);
        //spdlog::info("Generated URL without siteId: {}", urlWithoutSiteId);


        // Check if the URL with `siteId` exists
        if (checkUrlExists(urlWithSiteId)) {
            //spdlog::info("Valid URL with siteId found: {}", urlWithSiteId);
            return urlWithSiteId;  // Return URL with `siteId` if valid
        }

        // Check if the URL without `siteId` exists
        if (checkUrlExists(urlWithoutSiteId)) {
            //spdlog::info("Valid URL without siteId found: {}", urlWithoutSiteId);
            return urlWithoutSiteId;  // Return URL without `siteId` if valid
        }

        // If neither URL is valid, log the failure and return an empty string
        LOG_WARN("Neither URL with siteId nor URL without siteId exists.");

        return "";
    }
private:
    std::string region;
    std::string customerId;
    std::string siteId;
    std::string blobName;
    std::string sasToken;
    mutable std::unordered_map<std::string, bool> urlCache;
    std::unordered_map<std::string, std::string> regionSasTokens;  // Map of encrypted SAS tokens per region
    std::unordered_map<std::string, std::string> regionUrls;  // Map of region URLs
};

#endif  // URLGENERATOR_H
