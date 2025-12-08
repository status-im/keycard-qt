#pragma once

#include "channel_interface.h"
#include "apdu/command.h"
#include "apdu/response.h"
#include <QByteArray>
#include <QSharedPointer>
#include <QMutex>


namespace Keycard {

/**
 * @brief Secure channel for encrypted communication with keycard
 * 
 * Implements:
 * - ECDH key exchange (secp256k1)
 * - AES-CBC encryption/decryption
 * - CMAC for message authentication
 * - Session key derivation
 */
class SecureChannel {
public:
    /**
     * @brief Construct secure channel wrapping a base channel
     * @param channel Base channel for plain communication
     */
    explicit SecureChannel(IChannel* channel);
    
    ~SecureChannel();
    
    /**
     * @brief Generate ephemeral ECDH key pair and compute shared secret
     * @param cardPublicKey Card's public key (65 bytes, uncompressed)
     * @return true on success
     */
    bool generateSecret(const QByteArray& cardPublicKey);
    
    /**
     * @brief Initialize session keys
     * @param iv Initialization vector
     * @param encKey Encryption key (16 bytes for AES-128)
     * @param macKey MAC key (16 bytes)
     */
    void init(const QByteArray& iv, const QByteArray& encKey, const QByteArray& macKey);
    
    /**
     * @brief Reset the secure channel state
     */
    void reset();
    
    /**
     * @brief Get the raw public key for pairing
     * @return Public key bytes (65 bytes, uncompressed format)
     */
    QByteArray rawPublicKey() const;
    
    /**
     * @brief Get the shared secret
     * @return Shared secret bytes
     */
    QByteArray secret() const;
    
    /**
     * @brief Send a command through the secure channel
     * @param command APDU command (will be encrypted)
     * @return APDU response (decrypted)
     */
    APDU::Response send(const APDU::Command& command);
    
    /**
     * @brief Encrypt data using AES-CBC
     * @param plaintext Data to encrypt
     * @return Encrypted data
     */
    QByteArray encrypt(const QByteArray& plaintext);
    
    /**
     * @brief Decrypt data using AES-CBC
     * @param ciphertext Data to decrypt
     * @return Decrypted data
     */
    QByteArray decrypt(const QByteArray& ciphertext);
    
    /**
     * @brief One-shot encryption for initialization
     * @param data Data to encrypt
     * @return Encrypted data
     */
    QByteArray oneShotEncrypt(const QByteArray& data);
    
    /**
     * @brief Check if secure channel is open
     * @return true if initialized with session keys
     */
    bool isOpen() const;
    
private:
    struct Private;
    QSharedPointer<Private> d;
    
    // Thread safety - protects IV state during command encryption/transmission
    // Critical because IV is updated after each send() and multiple threads
    // may call CommandSet methods simultaneously (e.g. getStatus from UI thread
    // while authorize runs on worker thread)
    mutable QMutex m_secureMutex;
    
    // Helper methods
    QByteArray calculateMAC(const QByteArray& meta, const QByteArray& data);
    QByteArray updateMAC(const QByteArray& data);  // Legacy
    bool verifyMAC(const QByteArray& data, const QByteArray& receivedMAC);
};

} // namespace Keycard

