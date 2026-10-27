#pragma once

#include <QByteArray>
#include <QList>

namespace Keycard {
namespace ScV2 {

/**
 * @brief Cryptographic primitives of the Keycard applet 4.x SecureChannelV2
 *
 * - ECDH on secp256k1 between two ephemeral keys; the shared secret is the X coordinate
 * - HKDF-SHA256 (single block): PRK = HMAC(salt, X), OKM = HMAC(PRK, "sc_v2_ccm" || 0x01)
 * - AES-128-CCM, 13-byte nonce, 8-byte tag, no associated data
 * - Card authenticity: ECDSA-SHA256 transcript signature by the card identity key, whose
 *   certificate (identPub33 || r32 || s32 || v1) is a CA signature over SHA256(identPub)
 *
 * All functions return an empty value / false on failure (or when built without OpenSSL).
 */

constexpr int NonceSize = 13;
constexpr int TagSize = 8;
constexpr int SaltSize = 32;
constexpr int SessionKeySize = 16;
constexpr int PrivateKeySize = 32;
constexpr int PublicKeySize = 65;            ///< Uncompressed: 0x04 || X || Y
constexpr int CompressedPublicKeySize = 33;
constexpr int CertificateSize = 98;
constexpr int MaxPlaintextSize = 247;        ///< Applet limit for a wrapped APDU / response

/// "sc_v2_ccm": HKDF info prefix and transcript prefix
QByteArray label();

struct KeyPair {
    QByteArray privateKey;  ///< 32 bytes
    QByteArray publicKey;   ///< 65 bytes, uncompressed
    bool isValid() const { return privateKey.size() == PrivateKeySize && publicKey.size() == PublicKeySize; }
};

/// Fresh random secp256k1 key pair
KeyPair generateKeyPair();

/// Key pair for a known private key (test vectors, emulators)
KeyPair keyPairFromPrivateKey(const QByteArray& privateKey);

/// X coordinate of privateKey * peerPublicKey (peer key compressed or uncompressed)
QByteArray ecdhSharedSecret(const QByteArray& privateKey, const QByteArray& peerPublicKey);

struct SessionKeys {
    QByteArray hostToCard;  ///< keyH2C, 16 bytes
    QByteArray cardToHost;  ///< keyC2H, 16 bytes
};

/// HKDF-SHA256 of the ECDH secret, salted with the host's OPEN_SECURE_CHANNEL salt
SessionKeys deriveSessionKeys(const QByteArray& salt, const QByteArray& sharedSecret);

/// 13-byte big-endian encoding of the message counter
QByteArray nonce(quint64 counter);

/// AES-128-CCM: returns ciphertext || tag(8)
QByteArray ccmEncrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& plaintext);

/// AES-128-CCM: verifies the trailing tag; false on tag mismatch
bool ccmDecrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& ciphertextAndTag,
                QByteArray& plaintext);

/// SHA256("sc_v2_ccm" || salt || hostPublicKey || cardPublicKey): the digest the card signs
QByteArray transcriptDigest(const QByteArray& salt, const QByteArray& hostPublicKey, const QByteArray& cardPublicKey);

/// Verify a DER-encoded ECDSA signature of a 32-byte digest
bool verifySignature(const QByteArray& publicKey, const QByteArray& digest, const QByteArray& derSignature);

/// Recover the compressed public key that produced (r, s) over digest, recId 0..3
QByteArray recoverPublicKey(const QByteArray& digest, const QByteArray& r, const QByteArray& s, int recId);

/// Compressed form of a public key (accepts compressed or uncompressed input)
QByteArray compressPublicKey(const QByteArray& publicKey);

struct RecoverableSignature {
    QByteArray r;     ///< 32 bytes
    QByteArray s;     ///< 32 bytes
    int recId = -1;   ///< 0..3
    QByteArray der;   ///< DER encoding of (r, s)
    bool isValid() const { return recId >= 0; }
};

/// ECDSA-sign a 32-byte digest. Hosts never sign; card emulators and test vectors do.
RecoverableSignature sign(const QByteArray& privateKey, const QByteArray& digest);

/**
 * @brief Card identity certificate: identPub(33) || r(32) || s(32) || v(1)
 */
struct Certificate {
    QByteArray identityPublicKey;
    QByteArray r;
    QByteArray s;
    int recId = -1;

    static Certificate parse(const QByteArray& data);
    bool isValid() const { return identityPublicKey.size() == CompressedPublicKeySize && recId >= 0; }

    /// Compressed CA public key that signed SHA256(identityPublicKey), empty if unrecoverable
    QByteArray recoverCaPublicKey() const;

    /// True if the recovered CA key is one of trustedCaKeys (compressed or uncompressed)
    bool isTrustedBy(const QList<QByteArray>& trustedCaKeys) const;

    /// Build a certificate signed by caPrivateKey (card emulators and test vectors)
    static QByteArray create(const QByteArray& caPrivateKey, const QByteArray& identityPublicKey);
};

} // namespace ScV2
} // namespace Keycard
