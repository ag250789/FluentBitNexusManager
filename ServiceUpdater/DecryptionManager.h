#ifndef DECRYPTIONMANAGER_H
#define DECRYPTIONMANAGER_H

#include <sodium.h>
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <spdlog/spdlog.h>



/**
 * @brief The DecryptionManager class is responsible for decrypting fields using
 *        a predefined key. It utilizes libsodium for secure decryption and memory management.
 */
class DecryptionManager {
public:
    /**
     * @brief Constructor initializes the decryption key from a hardcoded hexadecimal string.
     *        It splits the key into two parts, combines them, and converts them into bytes.
     *
     * @throws std::runtime_error if the key size is invalid after conversion.
     */
    DecryptionManager() {
        std::string key_part1 = "9c75aee2371355b3197bf474ae6d6ebf";
        std::string key_part2 = "4a3bfcb70f94aaf4d1a30ff298c11e34";
        const std::string hex_key = key_part1 + key_part2;

        auto key_bytes = hex_to_bytes(hex_key);
        if (key_bytes.size() != KEY_LEN) {
            throw std::runtime_error("Invalid key size");
        }
        std::copy(key_bytes.begin(), key_bytes.end(), key);

        // Clear sensitive data from memory
        key_part1.clear();
        key_part2.clear();
        sodium_memzero(&key_part1, key_part1.size());
        sodium_memzero(&key_part2, key_part2.size());
        //spdlog::info("DecryptionManager initialized successfully");
        LOG_INFO("DecryptionManager initialized successfully");

    }

    /**
     * @brief Destructor clears the decryption key from memory to prevent sensitive data leaks.
     */
    ~DecryptionManager() {
        sodium_memzero(key, sizeof(key));
        //spdlog::info("DecryptionManager key erased and object destroyed");
        LOG_INFO("DecryptionManager key erased and object destroyed");

    }

    /**
     * @brief Decrypts a given hexadecimal-encoded encrypted field.
     *
     * @param encrypted_hex The encrypted data in hexadecimal format.
     * @return A decrypted string if successful, or an empty string if decryption fails.
     */
    std::string decrypt_field(const std::string& encrypted_hex) {
        auto encrypted_data = hex_to_bytes(encrypted_hex);
        if (encrypted_data.empty()) {
            //spdlog::error("Empty encrypted data passed for decryption");
            LOG_ERROR("Empty encrypted data passed for decryption");
            return "";
        }

        std::vector<unsigned char> decrypted_data;
        if (!decrypt_data(encrypted_data, decrypted_data)) {
            //spdlog::error("Failed to decrypt field.");
            LOG_ERROR("Failed to decrypt field.");
            return "";
        }

        return std::string(decrypted_data.begin(), decrypted_data.end());
    }

private:
    static const size_t KEY_LEN = 32;  // Key length in bytes (256-bit key)
    static const size_t NONCE_LEN = crypto_secretbox_NONCEBYTES;  // Nonce length as defined by libsodium
    static const size_t MAC_LEN = crypto_secretbox_MACBYTES;  // Message Authentication Code length (MAC)
    unsigned char key[KEY_LEN];  // The decryption key used for secretbox encryption/decryption

    /**
     * @brief Converts a hexadecimal string into a vector of bytes.
     *
     * @param hex_str The hexadecimal string to convert.
     * @return A vector of bytes representing the hex string.
     * @throws std::runtime_error if the conversion fails.
     */

    std::vector<unsigned char> hex_to_bytes(const std::string& hex_str) {
        std::vector<unsigned char> bytes(hex_str.size() / 2);
        if (sodium_hex2bin(bytes.data(), bytes.size(), hex_str.c_str(), hex_str.size(), NULL, NULL, NULL) != 0) {
            throw std::runtime_error("Failed to convert hex to bytes");
        }
        return bytes;
    }

    /**
     * @brief Decrypts the provided encrypted data using the stored key and nonce.
     *
     * @param encrypted_data The encrypted data to decrypt, containing the nonce and ciphertext.
     * @param decrypted_data The output vector to store decrypted data.
     * @return true if decryption is successful, false otherwise.
     */
    bool decrypt_data(const std::vector<unsigned char>& encrypted_data, std::vector<unsigned char>& decrypted_data) const {

        if (encrypted_data.size() < NONCE_LEN + MAC_LEN) {
            //spdlog::error("Encrypted data is too small for decryption.");
            LOG_ERROR("Encrypted data is too small for decryption.");
            return false;
        }

        unsigned char nonce[NONCE_LEN];
        memcpy(nonce, encrypted_data.data(), NONCE_LEN);

        size_t decrypted_size = encrypted_data.size() - NONCE_LEN - MAC_LEN;
        decrypted_data.resize(decrypted_size);

        if (crypto_secretbox_open_easy(decrypted_data.data(), encrypted_data.data() + NONCE_LEN, encrypted_data.size() - NONCE_LEN, nonce, key) != 0) {
            //spdlog::error("Decryption process failed.");
            LOG_ERROR("Decryption process failed.");
            return false;
        }

        return true;
    }
};

#endif // DECRYPTIONMANAGER_H
