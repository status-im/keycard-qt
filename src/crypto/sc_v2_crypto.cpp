#include "keycard-qt/sc_v2_crypto.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QMessageAuthenticationCode>
#include <memory>

#ifdef KEYCARD_QT_HAS_OPENSSL
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#endif

namespace Keycard {
namespace ScV2 {

QByteArray label()
{
    return QByteArrayLiteral("sc_v2_ccm");
}

SessionKeys deriveSessionKeys(const QByteArray& salt, const QByteArray& sharedSecret)
{
    // HKDF-SHA256 extract + one expand block (32 bytes = both 16-byte keys)
    const QByteArray prk = QMessageAuthenticationCode::hash(sharedSecret, salt, QCryptographicHash::Sha256);
    const QByteArray okm = QMessageAuthenticationCode::hash(label() + QByteArray(1, '\x01'), prk,
                                                            QCryptographicHash::Sha256);
    return { okm.left(SessionKeySize), okm.mid(SessionKeySize, SessionKeySize) };
}

QByteArray nonce(quint64 counter)
{
    QByteArray out(NonceSize, '\0');
    for (int i = NonceSize - 1; i >= 0 && counter != 0; --i) {
        out[i] = static_cast<char>(counter & 0xFF);
        counter >>= 8;
    }
    return out;
}

QByteArray transcriptDigest(const QByteArray& salt, const QByteArray& hostPublicKey, const QByteArray& cardPublicKey)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(label());
    hash.addData(salt);
    hash.addData(hostPublicKey);
    hash.addData(cardPublicKey);
    return hash.result();
}

Certificate Certificate::parse(const QByteArray& data)
{
    Certificate cert;
    if (data.size() != CertificateSize) {
        return cert;
    }
    cert.identityPublicKey = data.left(CompressedPublicKeySize);
    cert.r = data.mid(CompressedPublicKeySize, 32);
    cert.s = data.mid(CompressedPublicKeySize + 32, 32);
    int v = static_cast<uint8_t>(data[CertificateSize - 1]);
    if (v >= 27) {
        v -= 27;  // Ethereum-style encoding
    }
    if (v <= 3) {
        cert.recId = v;
    }
    return cert;
}

QByteArray Certificate::recoverCaPublicKey() const
{
    if (!isValid()) {
        return QByteArray();
    }
    const QByteArray digest = QCryptographicHash::hash(identityPublicKey, QCryptographicHash::Sha256);
    return recoverPublicKey(digest, r, s, recId);
}

bool Certificate::isTrustedBy(const QList<QByteArray>& trustedCaKeys) const
{
    const QByteArray caKey = recoverCaPublicKey();
    if (caKey.isEmpty()) {
        return false;
    }
    for (const QByteArray& trusted : trustedCaKeys) {
        if (compressPublicKey(trusted) == caKey) {
            return true;
        }
    }
    return false;
}

QByteArray Certificate::create(const QByteArray& caPrivateKey, const QByteArray& identityPublicKey)
{
    const QByteArray identity = compressPublicKey(identityPublicKey);
    const QByteArray digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
    const RecoverableSignature sig = sign(caPrivateKey, digest);
    if (identity.isEmpty() || !sig.isValid()) {
        return QByteArray();
    }
    return identity + sig.r + sig.s + QByteArray(1, static_cast<char>(sig.recId));
}

#ifdef KEYCARD_QT_HAS_OPENSSL

namespace {

using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using PointPtr = std::unique_ptr<EC_POINT, decltype(&EC_POINT_free)>;
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using SigPtr = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;

struct Curve {
    std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)> group{EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free};
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx{BN_CTX_new(), BN_CTX_free};

    bool isValid() const { return group && ctx; }
    const BIGNUM* order() const { return EC_GROUP_get0_order(group.get()); }
    PointPtr newPoint() const { return PointPtr(EC_POINT_new(group.get()), EC_POINT_free); }
};

BnPtr newBn()
{
    return BnPtr(BN_new(), BN_free);
}

BnPtr toBn(const QByteArray& bytes)
{
    return BnPtr(BN_bin2bn(reinterpret_cast<const unsigned char*>(bytes.constData()), bytes.size(), nullptr), BN_free);
}

QByteArray fromBn(const BIGNUM* bn)
{
    QByteArray out(32, '\0');
    if (BN_bn2binpad(bn, reinterpret_cast<unsigned char*>(out.data()), out.size()) != out.size()) {
        return QByteArray();
    }
    return out;
}

PointPtr decodePoint(const Curve& curve, const QByteArray& encoded)
{
    PointPtr point = curve.newPoint();
    if (!point || encoded.isEmpty()
        || EC_POINT_oct2point(curve.group.get(), point.get(),
                              reinterpret_cast<const unsigned char*>(encoded.constData()),
                              encoded.size(), curve.ctx.get()) != 1) {
        return PointPtr(nullptr, EC_POINT_free);
    }
    return point;
}

QByteArray encodePoint(const Curve& curve, const EC_POINT* point, point_conversion_form_t form)
{
    const size_t len = EC_POINT_point2oct(curve.group.get(), point, form, nullptr, 0, curve.ctx.get());
    if (len == 0) {
        return QByteArray();
    }
    QByteArray out(static_cast<int>(len), '\0');
    EC_POINT_point2oct(curve.group.get(), point, form, reinterpret_cast<unsigned char*>(out.data()), len,
                       curve.ctx.get());
    return out;
}

/// 0 < value < order
bool isScalarInRange(const Curve& curve, const BIGNUM* value)
{
    return !BN_is_zero(value) && !BN_is_negative(value) && BN_cmp(value, curve.order()) < 0;
}

/// Digest as a scalar mod n
BnPtr digestScalar(const Curve& curve, const QByteArray& digest)
{
    BnPtr e = toBn(digest);
    if (e && BN_nnmod(e.get(), e.get(), curve.order(), curve.ctx.get()) != 1) {
        return BnPtr(nullptr, BN_free);
    }
    return e;
}

} // anonymous namespace

KeyPair keyPairFromPrivateKey(const QByteArray& privateKey)
{
    Curve curve;
    if (!curve.isValid() || privateKey.size() != PrivateKeySize) {
        return KeyPair();
    }
    BnPtr d = toBn(privateKey);
    PointPtr publicPoint = curve.newPoint();
    if (!d || !publicPoint || !isScalarInRange(curve, d.get())
        || EC_POINT_mul(curve.group.get(), publicPoint.get(), d.get(), nullptr, nullptr, curve.ctx.get()) != 1) {
        return KeyPair();
    }
    return { privateKey, encodePoint(curve, publicPoint.get(), POINT_CONVERSION_UNCOMPRESSED) };
}

KeyPair generateKeyPair()
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        QByteArray privateKey(PrivateKeySize, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(privateKey.data()), privateKey.size()) != 1) {
            return KeyPair();
        }
        KeyPair pair = keyPairFromPrivateKey(privateKey);
        if (pair.isValid()) {
            return pair;
        }
    }
    return KeyPair();
}

QByteArray ecdhSharedSecret(const QByteArray& privateKey, const QByteArray& peerPublicKey)
{
    Curve curve;
    if (!curve.isValid()) {
        return QByteArray();
    }
    BnPtr d = toBn(privateKey);
    PointPtr peer = decodePoint(curve, peerPublicKey);
    PointPtr shared = curve.newPoint();
    BnPtr x = newBn();
    if (!d || !peer || !shared || !x || !isScalarInRange(curve, d.get())
        || EC_POINT_mul(curve.group.get(), shared.get(), nullptr, peer.get(), d.get(), curve.ctx.get()) != 1
        || EC_POINT_is_at_infinity(curve.group.get(), shared.get())
        || EC_POINT_get_affine_coordinates(curve.group.get(), shared.get(), x.get(), nullptr, curve.ctx.get()) != 1) {
        return QByteArray();
    }
    return fromBn(x.get());
}

QByteArray ccmEncrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& plaintext)
{
    if (key.size() != SessionKeySize || nonce.size() != NonceSize) {
        return QByteArray();
    }
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray out(plaintext.size() + TagSize, '\0');
    auto* outData = reinterpret_cast<unsigned char*>(out.data());
    int len = 0;
    if (!ctx
        || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, NonceSize, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, TagSize, nullptr) != 1
        || EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
                              reinterpret_cast<const unsigned char*>(key.constData()),
                              reinterpret_cast<const unsigned char*>(nonce.constData())) != 1
        // CCM needs the total length up front
        || EVP_EncryptUpdate(ctx.get(), nullptr, &len, nullptr, plaintext.size()) != 1
        || EVP_EncryptUpdate(ctx.get(), outData, &len,
                             reinterpret_cast<const unsigned char*>(plaintext.constData()), plaintext.size()) != 1
        || EVP_EncryptFinal_ex(ctx.get(), outData + len, &len) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, TagSize, outData + plaintext.size()) != 1) {
        return QByteArray();
    }
    return out;
}

bool ccmDecrypt(const QByteArray& key, const QByteArray& nonce, const QByteArray& ciphertextAndTag,
                QByteArray& plaintext)
{
    plaintext.clear();
    if (key.size() != SessionKeySize || nonce.size() != NonceSize || ciphertextAndTag.size() < TagSize) {
        return false;
    }
    const int ciphertextSize = ciphertextAndTag.size() - TagSize;
    QByteArray tag = ciphertextAndTag.right(TagSize);
    QByteArray out(ciphertextSize, '\0');
    CipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    int len = 0;
    if (!ctx
        || EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, NonceSize, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, TagSize, tag.data()) != 1
        || EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                              reinterpret_cast<const unsigned char*>(key.constData()),
                              reinterpret_cast<const unsigned char*>(nonce.constData())) != 1
        || EVP_DecryptUpdate(ctx.get(), nullptr, &len, nullptr, ciphertextSize) != 1
        // In CCM mode this update also verifies the tag
        || EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &len,
                             reinterpret_cast<const unsigned char*>(ciphertextAndTag.constData()),
                             ciphertextSize) != 1) {
        return false;
    }
    plaintext = out;
    return true;
}

bool verifySignature(const QByteArray& publicKey, const QByteArray& digest, const QByteArray& derSignature)
{
    Curve curve;
    if (!curve.isValid() || digest.size() != 32 || derSignature.isEmpty()) {
        return false;
    }
    const auto* derData = reinterpret_cast<const unsigned char*>(derSignature.constData());
    SigPtr sig(d2i_ECDSA_SIG(nullptr, &derData, derSignature.size()), ECDSA_SIG_free);
    PointPtr q = decodePoint(curve, publicKey);
    if (!sig || !q) {
        return false;
    }
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    if (!isScalarInRange(curve, r) || !isScalarInRange(curve, s)) {
        return false;
    }

    // R = (e * s^-1) G + (r * s^-1) Q; valid iff R.x mod n == r
    BnPtr e = digestScalar(curve, digest);
    BnPtr w = newBn();
    BnPtr u1 = newBn();
    BnPtr u2 = newBn();
    BnPtr x = newBn();
    PointPtr point = curve.newPoint();
    BN_CTX* ctx = curve.ctx.get();
    if (!e || !w || !u1 || !u2 || !x || !point
        || !BN_mod_inverse(w.get(), s, curve.order(), ctx)
        || BN_mod_mul(u1.get(), e.get(), w.get(), curve.order(), ctx) != 1
        || BN_mod_mul(u2.get(), r, w.get(), curve.order(), ctx) != 1
        || EC_POINT_mul(curve.group.get(), point.get(), u1.get(), q.get(), u2.get(), ctx) != 1
        || EC_POINT_is_at_infinity(curve.group.get(), point.get())
        || EC_POINT_get_affine_coordinates(curve.group.get(), point.get(), x.get(), nullptr, ctx) != 1
        || BN_nnmod(x.get(), x.get(), curve.order(), ctx) != 1) {
        return false;
    }
    return BN_cmp(x.get(), r) == 0;
}

QByteArray recoverPublicKey(const QByteArray& digest, const QByteArray& r, const QByteArray& s, int recId)
{
    Curve curve;
    if (!curve.isValid() || digest.size() != 32 || r.size() != 32 || s.size() != 32 || recId < 0 || recId > 3) {
        return QByteArray();
    }
    BN_CTX* ctx = curve.ctx.get();
    BnPtr rBn = toBn(r);
    BnPtr sBn = toBn(s);
    BnPtr e = digestScalar(curve, digest);
    BnPtr x = newBn();
    BnPtr p = newBn();
    BnPtr rInv = newBn();
    BnPtr u1 = newBn();
    BnPtr u2 = newBn();
    PointPtr bigR = curve.newPoint();
    PointPtr q = curve.newPoint();
    if (!rBn || !sBn || !e || !x || !p || !rInv || !u1 || !u2 || !bigR || !q
        || !isScalarInRange(curve, rBn.get()) || !isScalarInRange(curve, sBn.get())
        || EC_GROUP_get_curve(curve.group.get(), p.get(), nullptr, nullptr, ctx) != 1) {
        return QByteArray();
    }

    // R.x = r + (recId / 2) * n, with y parity recId & 1 (SEC 1, 4.1.6)
    if (BN_copy(x.get(), rBn.get()) == nullptr
        || (recId >= 2 && BN_add(x.get(), x.get(), curve.order()) != 1)
        || BN_cmp(x.get(), p.get()) >= 0
        || EC_POINT_set_compressed_coordinates(curve.group.get(), bigR.get(), x.get(), recId & 1, ctx) != 1) {
        return QByteArray();
    }

    // Q = r^-1 (s R - e G) = (-e r^-1) G + (s r^-1) R
    if (!BN_mod_inverse(rInv.get(), rBn.get(), curve.order(), ctx)
        || BN_mod_sub(u1.get(), curve.order(), e.get(), curve.order(), ctx) != 1
        || BN_mod_mul(u1.get(), u1.get(), rInv.get(), curve.order(), ctx) != 1
        || BN_mod_mul(u2.get(), sBn.get(), rInv.get(), curve.order(), ctx) != 1
        || EC_POINT_mul(curve.group.get(), q.get(), u1.get(), bigR.get(), u2.get(), ctx) != 1
        || EC_POINT_is_at_infinity(curve.group.get(), q.get())) {
        return QByteArray();
    }
    return encodePoint(curve, q.get(), POINT_CONVERSION_COMPRESSED);
}

QByteArray compressPublicKey(const QByteArray& publicKey)
{
    Curve curve;
    if (!curve.isValid()) {
        return QByteArray();
    }
    PointPtr point = decodePoint(curve, publicKey);
    return point ? encodePoint(curve, point.get(), POINT_CONVERSION_COMPRESSED) : QByteArray();
}

RecoverableSignature sign(const QByteArray& privateKey, const QByteArray& digest)
{
    Curve curve;
    RecoverableSignature result;
    if (!curve.isValid() || digest.size() != 32) {
        return result;
    }
    BN_CTX* ctx = curve.ctx.get();
    BnPtr d = toBn(privateKey);
    BnPtr e = digestScalar(curve, digest);
    if (!d || !e || !isScalarInRange(curve, d.get())) {
        return result;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        KeyPair nonceKey = generateKeyPair();
        if (!nonceKey.isValid()) {
            return result;
        }
        BnPtr k = toBn(nonceKey.privateKey);
        PointPtr bigR = decodePoint(curve, nonceKey.publicKey);
        BnPtr x = newBn();
        BnPtr y = newBn();
        BnPtr r = newBn();
        BnPtr s = newBn();
        BnPtr kInv = newBn();
        if (!k || !bigR || !x || !y || !r || !s || !kInv
            || EC_POINT_get_affine_coordinates(curve.group.get(), bigR.get(), x.get(), y.get(), ctx) != 1
            || BN_nnmod(r.get(), x.get(), curve.order(), ctx) != 1) {
            return result;
        }
        if (BN_is_zero(r.get())) {
            continue;
        }
        // s = k^-1 (e + r d) mod n
        if (BN_mod_mul(s.get(), r.get(), d.get(), curve.order(), ctx) != 1
            || BN_mod_add(s.get(), s.get(), e.get(), curve.order(), ctx) != 1
            || !BN_mod_inverse(kInv.get(), k.get(), curve.order(), ctx)
            || BN_mod_mul(s.get(), s.get(), kInv.get(), curve.order(), ctx) != 1) {
            return result;
        }
        if (BN_is_zero(s.get())) {
            continue;
        }

        result.r = fromBn(r.get());
        result.s = fromBn(s.get());
        result.recId = (BN_is_odd(y.get()) ? 1 : 0) | (BN_cmp(x.get(), curve.order()) >= 0 ? 2 : 0);

        SigPtr sig(ECDSA_SIG_new(), ECDSA_SIG_free);
        if (!sig || ECDSA_SIG_set0(sig.get(), r.release(), s.release()) != 1) {
            return RecoverableSignature();
        }
        const int derSize = i2d_ECDSA_SIG(sig.get(), nullptr);
        result.der.resize(derSize);
        auto* derData = reinterpret_cast<unsigned char*>(result.der.data());
        i2d_ECDSA_SIG(sig.get(), &derData);
        return result;
    }
    return RecoverableSignature();
}

#else // !KEYCARD_QT_HAS_OPENSSL

KeyPair keyPairFromPrivateKey(const QByteArray&) { return KeyPair(); }
KeyPair generateKeyPair() { return KeyPair(); }
QByteArray ecdhSharedSecret(const QByteArray&, const QByteArray&) { return QByteArray(); }
QByteArray ccmEncrypt(const QByteArray&, const QByteArray&, const QByteArray&) { return QByteArray(); }
bool ccmDecrypt(const QByteArray&, const QByteArray&, const QByteArray&, QByteArray& plaintext)
{
    plaintext.clear();
    return false;
}
bool verifySignature(const QByteArray&, const QByteArray&, const QByteArray&) { return false; }
QByteArray recoverPublicKey(const QByteArray&, const QByteArray&, const QByteArray&, int) { return QByteArray(); }
QByteArray compressPublicKey(const QByteArray&) { return QByteArray(); }
RecoverableSignature sign(const QByteArray&, const QByteArray&) { return RecoverableSignature(); }

#endif // KEYCARD_QT_HAS_OPENSSL

} // namespace ScV2
} // namespace Keycard
