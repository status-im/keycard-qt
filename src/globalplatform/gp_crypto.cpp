#include "keycard-qt/globalplatform/gp_crypto.h"
#include <QDebug>

#ifdef KEYCARD_QT_HAS_OPENSSL
#include <openssl/des.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#endif

namespace Keycard {
namespace GlobalPlatform {

QByteArray Crypto::appendDESPadding(const QByteArray& data, int blockSize)
{
    QByteArray padded = data;
    
    // Add 0x80 byte
    padded.append(static_cast<char>(0x80));
    
    // Add 0x00 bytes until we reach block boundary
    int paddingSize = blockSize - (padded.size() % blockSize);
    if (paddingSize < blockSize) {
        padded.append(QByteArray(paddingSize, 0x00));
    }
    
    return padded;
}

QByteArray Crypto::removeDESPadding(const QByteArray& data)
{
    if (data.isEmpty()) {
        return data;
    }
    
    // Find the 0x80 padding marker from the end
    int paddingStart = data.size() - 1;
    while (paddingStart >= 0 && static_cast<uint8_t>(data[paddingStart]) == 0x00) {
        paddingStart--;
    }
    
    if (paddingStart >= 0 && static_cast<uint8_t>(data[paddingStart]) == 0x80) {
        return data.left(paddingStart);
    }
    
    // No padding found, return as-is
    return data;
}

QByteArray Crypto::encrypt3DES_CBC(const QByteArray& key, const QByteArray& iv, const QByteArray& data)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for 3DES encryption";
    return QByteArray();
#else
    if (key.size() != 16) {
        qWarning() << "GP Crypto: Key must be 16 bytes for 3DES, got" << key.size();
        return QByteArray();
    }
    
    if (iv.size() != 8) {
        qWarning() << "GP Crypto: IV must be 8 bytes for 3DES, got" << iv.size();
        return QByteArray();
    }
    
    // Pad the data
    QByteArray padded = appendDESPadding(data);
    
    // Setup 3DES key (16 bytes = K1||K2, K3 = K1)
    QByteArray key24(24, 0);
    key24.replace(0, 8, key.left(8));   // K1
    key24.replace(8, 8, key.mid(8, 8)); // K2
    key24.replace(16, 8, key.left(8));  // K3 = K1
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "GP Crypto: Failed to create cipher context";
        return QByteArray();
    }
    
    // Initialize encryption with 3DES-CBC
    if (EVP_EncryptInit_ex(ctx, EVP_des_ede3_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key24.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        qWarning() << "GP Crypto: EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    // Disable padding (we already padded)
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    // Encrypt
    QByteArray encrypted(padded.size() + 16, 0);
    int len = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(padded.constData()),
                          padded.size()) != 1) {
        qWarning() << "GP Crypto: EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(encrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "GP Crypto: EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    encrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    return encrypted;
#endif
}

QByteArray Crypto::decrypt3DES_CBC(const QByteArray& key, const QByteArray& iv, const QByteArray& data)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for 3DES decryption";
    return QByteArray();
#else
    if (key.size() != 16) {
        qWarning() << "GP Crypto: Key must be 16 bytes for 3DES";
        return QByteArray();
    }
    
    if (iv.size() != 8) {
        qWarning() << "GP Crypto: IV must be 8 bytes for 3DES";
        return QByteArray();
    }
    
    if (data.isEmpty()) {
        return QByteArray();
    }
    
    // Setup 3DES key (K1||K2||K1)
    QByteArray key24(24, 0);
    key24.replace(0, 8, key.left(8));   // K1
    key24.replace(8, 8, key.mid(8, 8)); // K2
    key24.replace(16, 8, key.left(8));  // K3 = K1
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "GP Crypto: Failed to create cipher context for decryption";
        return QByteArray();
    }
    
    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_des_ede3_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key24.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        qWarning() << "GP Crypto: EVP_DecryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    // Disable padding (we'll unpad manually)
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    // Decrypt
    QByteArray decrypted(data.size() + 16, 0);
    int len = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(decrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(data.constData()),
                          data.size()) != 1) {
        qWarning() << "GP Crypto: EVP_DecryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(decrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "GP Crypto: EVP_DecryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    
    decrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    // Remove padding
    return removeDESPadding(decrypted);
#endif
}

QByteArray Crypto::mac3DES(const QByteArray& key, const QByteArray& data, const QByteArray& iv)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for MAC calculation";
    return QByteArray(8, 0x00);
#else
    if (key.size() != 16) {
        qWarning() << "GP Crypto: MAC key must be 16 bytes";
        return QByteArray(8, 0x00);
    }
    
    if (iv.size() != 8) {
        qWarning() << "GP Crypto: IV must be 8 bytes for MAC";
        return QByteArray(8, 0x00);
    }
    
    // Pad the data
    QByteArray padded = appendDESPadding(data);
    
    // Retail MAC uses single DES for all blocks except last, then 3DES for last block
    // For simplicity, we use full 3DES-CBC which is compatible
    
    // Setup 3DES key
    QByteArray key24(24, 0);
    key24.replace(0, 8, key.left(8));   // K1
    key24.replace(8, 8, key.mid(8, 8)); // K2
    key24.replace(16, 8, key.left(8));  // K3 = K1
    
    // Create cipher context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "GP Crypto: Failed to create cipher context for MAC";
        return QByteArray(8, 0x00);
    }
    
    // Initialize
    if (EVP_EncryptInit_ex(ctx, EVP_des_ede3_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key24.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        qWarning() << "GP Crypto: MAC EVP_EncryptInit_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray(8, 0x00);
    }
    
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    // Encrypt
    QByteArray encrypted(padded.size() + 16, 0);
    int len = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(padded.constData()),
                          padded.size()) != 1) {
        qWarning() << "GP Crypto: MAC EVP_EncryptUpdate failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray(8, 0x00);
    }
    
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(encrypted.data()) + len, &finalLen) != 1) {
        qWarning() << "GP Crypto: MAC EVP_EncryptFinal_ex failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray(8, 0x00);
    }
    
    encrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    // MAC is the last 8 bytes of the encrypted data
    return encrypted.right(8);
#endif
}

QByteArray Crypto::deriveKey(const QByteArray& key, const QByteArray& sequence, const QByteArray& purpose)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for key derivation";
    return QByteArray();
#else
    if (key.size() != 16) {
        qWarning() << "GP Crypto: Base key must be 16 bytes";
        return QByteArray();
    }
    
    if (sequence.size() != 2) {
        qWarning() << "GP Crypto: Sequence must be 2 bytes";
        return QByteArray();
    }
    
    if (purpose.size() != 2) {
        qWarning() << "GP Crypto: Purpose must be 2 bytes";
        return QByteArray();
    }
    
    // Derivation data: purpose[0:2] || sequence[0:2] || 00...00 (total 16 bytes)
    QByteArray derivationData(16, 0x00);
    derivationData[0] = purpose[0];
    derivationData[1] = purpose[1];
    derivationData[2] = sequence[0];
    derivationData[3] = sequence[1];
    
    // Encrypt derivation data with base key (null IV)
    QByteArray nullIV = NULL_BYTES_8();
    QByteArray derived = encrypt3DES_CBC(key, nullIV, derivationData);
    
    // Return first 16 bytes as derived key
    return derived.left(16);
#endif
}

bool Crypto::verifyCryptogram(const QByteArray& encKey, const QByteArray& hostChallenge, 
                               const QByteArray& cardChallenge, const QByteArray& cardCryptogram)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for cryptogram verification";
    return false;
#else
    qDebug() << "GP Crypto: Verifying cryptogram:";
    qDebug() << "  ENC key:" << encKey.toHex();
    qDebug() << "  Host challenge:" << hostChallenge.toHex();
    qDebug() << "  Card challenge:" << cardChallenge.toHex();
    qDebug() << "  Card cryptogram:" << cardCryptogram.toHex();
    
    // Concatenate challenges
    QByteArray data = hostChallenge + cardChallenge;
    qDebug() << "  Combined data:" << data.toHex();
    
    // Calculate MAC (mac3DES will handle padding internally)
    QByteArray calculated = mac3DES(encKey, data, NULL_BYTES_8());
    qDebug() << "  Calculated MAC:" << calculated.toHex();
    qDebug() << "  Expected MAC:" << cardCryptogram.toHex();
    
    // Compare
    bool matches = (calculated == cardCryptogram);
    qDebug() << "  Match:" << matches;
    return matches;
#endif
}

QByteArray Crypto::macFull3DES(const QByteArray& key, const QByteArray& data, const QByteArray& iv)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for full 3DES MAC";
    return QByteArray(8, 0x00);
#else
    if (key.size() != 16) {
        qWarning() << "GP Crypto: MAC key must be 16 bytes";
        return QByteArray(8, 0x00);
    }
    
    if (iv.size() != 8) {
        qWarning() << "GP Crypto: IV must be 8 bytes";
        return QByteArray(8, 0x00);
    }
    
    // Pad the data
    QByteArray paddedData = appendDESPadding(data);
    
    qDebug() << "GP Crypto: macFull3DES - TRUE RETAIL MAC (single DES + 3DES)";
    
    // RETAIL MAC: Single DES for intermediate blocks, 3DES for last block
    // This matches the Go implementation and GlobalPlatform SCP02 spec
    
    QByteArray currentIV = iv;
    QByteArray singleDESKey = key.left(8);  // First 8 bytes
    
    // Process all blocks except the last with single DES-CBC
    if (paddedData.size() > 8) {
        int intermediateLength = paddedData.size() - 8;
        QByteArray intermediateData = paddedData.left(intermediateLength);
        
        // Load OpenSSL legacy provider for DES (required for OpenSSL 3.x)
        OSSL_PROVIDER* legacy = OSSL_PROVIDER_load(nullptr, "legacy");
        OSSL_PROVIDER* deflt = OSSL_PROVIDER_load(nullptr, "default");
        
        // Single DES CBC for intermediate blocks
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            if (legacy) OSSL_PROVIDER_unload(legacy);
            if (deflt) OSSL_PROVIDER_unload(deflt);
            qWarning() << "GP Crypto: Failed to create cipher context";
            return QByteArray(8, 0x00);
        }
        
        if (EVP_EncryptInit_ex(ctx, EVP_des_cbc(), nullptr,
                               (const unsigned char*)singleDESKey.constData(),
                               (const unsigned char*)currentIV.constData()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            if (legacy) OSSL_PROVIDER_unload(legacy);
            if (deflt) OSSL_PROVIDER_unload(deflt);
            qWarning() << "GP Crypto: Single DES init failed";
            return QByteArray(8, 0x00);
        }
        
        EVP_CIPHER_CTX_set_padding(ctx, 0);  // No padding, already padded
        
        QByteArray encrypted(intermediateLength + 16, 0);
        int outlen = 0;
        
        if (EVP_EncryptUpdate(ctx, (unsigned char*)encrypted.data(), &outlen,
                             (const unsigned char*)intermediateData.constData(),
                             intermediateLength) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            if (legacy) OSSL_PROVIDER_unload(legacy);
            if (deflt) OSSL_PROVIDER_unload(deflt);
            qWarning() << "GP Crypto: Single DES encrypt failed";
            return QByteArray(8, 0x00);
        }
        
        EVP_CIPHER_CTX_free(ctx);
        if (legacy) OSSL_PROVIDER_unload(legacy);
        if (deflt) OSSL_PROVIDER_unload(deflt);
        
        encrypted.resize(outlen);
        currentIV = encrypted.right(8);  // Last 8 bytes become IV for 3DES
    }
    
    // Process last block with 3DES-CBC
    QByteArray lastBlock = paddedData.right(8);
    QByteArray result = encrypt3DES_CBC(key, currentIV, lastBlock);
    
    if (result.isEmpty() || result.size() < 8) {
        qWarning() << "GP Crypto: 3DES final block failed";
        return QByteArray(8, 0x00);
    }
    
    QByteArray mac = result.left(8);
    qDebug() << "GP Crypto: macFull3DES result:" << mac.toHex();
    
    return mac;
#endif
}

QByteArray Crypto::encryptICV(const QByteArray& macKey, const QByteArray& icv)
{
#ifndef KEYCARD_QT_HAS_OPENSSL
    qWarning() << "GP Crypto: OpenSSL not available for ICV encryption";
    return QByteArray(8, 0x00);
#else
    if (icv.size() != 8) {
        qWarning() << "GP Crypto: ICV must be 8 bytes";
        return QByteArray(8, 0x00);
    }
    
    // Use first 8 bytes of MAC key for single DES
    QByteArray singleDESKey = macKey.left(8);
    QByteArray nullIV = NULL_BYTES_8();
    
    // Create single DES cipher
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        qWarning() << "GP Crypto: Failed to create cipher context for ICV encryption";
        return QByteArray(8, 0x00);
    }
    
    if (EVP_EncryptInit_ex(ctx, EVP_des_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(singleDESKey.constData()),
                           reinterpret_cast<const unsigned char*>(nullIV.constData())) != 1) {
        qWarning() << "GP Crypto: ICV encryption init failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray(8, 0x00);
    }
    
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    
    QByteArray encrypted(16, 0);
    int len = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(encrypted.data()), &len,
                          reinterpret_cast<const unsigned char*>(icv.constData()),
                          icv.size()) != 1) {
        qWarning() << "GP Crypto: ICV encryption update failed";
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray(8, 0x00);
    }
    
    int finalLen = 0;
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(encrypted.data()) + len, &finalLen);
    encrypted.resize(len + finalLen);
    EVP_CIPHER_CTX_free(ctx);
    
    return encrypted.left(8);
#endif
}

} // namespace GlobalPlatform
} // namespace Keycard

