#pragma once

#include <QByteArray>

namespace Keycard {
namespace GlobalPlatform {

/**
 * @brief 3DES encryption/decryption utilities for SCP02
 * 
 * SCP02 uses 3DES (Triple DES) for encryption and MAC calculation.
 * This is different from Keycard's AES-256.
 */
class Crypto {
public:
    /**
     * @brief Encrypt data using 3DES in CBC mode
     * @param key 16-byte 3DES key (K1||K2, third key K3 = K1)
     * @param iv 8-byte initialization vector
     * @param data Data to encrypt (will be padded)
     * @return Encrypted data
     */
    static QByteArray encrypt3DES_CBC(const QByteArray& key, const QByteArray& iv, const QByteArray& data);
    
    /**
     * @brief Decrypt data using 3DES in CBC mode
     * @param key 16-byte 3DES key
     * @param iv 8-byte initialization vector
     * @param data Data to decrypt
     * @return Decrypted data (with padding removed)
     */
    static QByteArray decrypt3DES_CBC(const QByteArray& key, const QByteArray& iv, const QByteArray& data);
    
    /**
     * @brief Calculate MAC using 3DES (retail MAC)
     * @param key 16-byte MAC key
     * @param data Data to calculate MAC over
     * @param iv 8-byte initialization vector (default: null bytes)
     * @return 8-byte MAC
     */
    static QByteArray mac3DES(const QByteArray& key, const QByteArray& data, const QByteArray& iv = QByteArray(8, 0x00));
    
    /**
     * @brief Append DES padding (ISO 9797-1 Method 2)
     * Adds 0x80 followed by 0x00 bytes to reach block boundary
     * @param data Data to pad
     * @param blockSize Block size (default: 8 bytes for DES)
     * @return Padded data
     */
    static QByteArray appendDESPadding(const QByteArray& data, int blockSize = 8);
    
    /**
     * @brief Remove DES padding
     * @param data Padded data
     * @return Data with padding removed
     */
    static QByteArray removeDESPadding(const QByteArray& data);
    
    /**
     * @brief Derive session keys from base key and sequence counter
     * @param key Base key (16 bytes)
     * @param sequence Sequence counter (2 bytes)
     * @param purpose Derivation purpose (2 bytes: {0x01, 0x82} for ENC, {0x01, 0x01} for MAC)
     * @return Derived key (16 bytes)
     */
    static QByteArray deriveKey(const QByteArray& key, const QByteArray& sequence, const QByteArray& purpose);
    
    /**
     * @brief Verify card cryptogram during authentication
     * @param encKey Encryption key
     * @param hostChallenge Host challenge (8 bytes)
     * @param cardChallenge Card challenge (8 bytes)
     * @param cardCryptogram Card cryptogram to verify (8 bytes)
     * @return true if cryptogram is valid
     */
    static bool verifyCryptogram(const QByteArray& encKey, const QByteArray& hostChallenge, 
                                 const QByteArray& cardChallenge, const QByteArray& cardCryptogram);
    
    /**
     * @brief Calculate full 3DES MAC (retail MAC: single DES then 3DES for last block)
     * @param key MAC key (16 bytes)
     * @param data Data to MAC
     * @param iv Initialization vector (8 bytes)
     * @return 8-byte MAC
     */
    static QByteArray macFull3DES(const QByteArray& key, const QByteArray& data, const QByteArray& iv);
    
    /**
     * @brief Encrypt ICV (Initialization Chaining Vector) with single DES
     * Used for MAC chaining in SCP02
     * @param macKey MAC key (first 8 bytes used)
     * @param icv Current ICV (8 bytes)
     * @return Encrypted ICV (8 bytes)
     */
    static QByteArray encryptICV(const QByteArray& macKey, const QByteArray& icv);
    
    // Derivation purposes (matching Go crypto package)
    static inline QByteArray DERIVATION_PURPOSE_ENC() { return QByteArray::fromHex("0182"); }
    static inline QByteArray DERIVATION_PURPOSE_MAC() { return QByteArray::fromHex("0101"); }
    static inline QByteArray NULL_BYTES_8() { return QByteArray(8, 0x00); }
};

} // namespace GlobalPlatform
} // namespace Keycard

