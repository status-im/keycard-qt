#include <QTest>
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/sc_v2_crypto.h"
#include "keycard-qt/secure_channel_v2.h"
#include "keycard-qt/types_parser.h"
#include "mocks/mock_backend.h"
#include "mocks/scv2_card_emulator.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QMutex>
#include <QSemaphore>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace Keycard;
using namespace Keycard::Test;

namespace {

/// Card side of an open SecureChannelV2 session whose transmit() blocks until released
class BlockingScV2Channel : public IChannel {
public:
    explicit BlockingScV2Channel(const ScV2::SessionKeys& keys) : m_keys(keys) {}

    QByteArray transmit(const QByteArray& apdu) override {
        const int now = ++m_inFlight;
        int seen = m_maxInFlight.load();
        while (now > seen && !m_maxInFlight.compare_exchange_weak(seen, now)) {}
        {
            QMutexLocker locker(&m_mutex);
            sent.append(apdu);
        }
        entered.release();
        release.acquire();

        QByteArray reply = QByteArray::fromHex("6982");
        {
            QMutexLocker locker(&m_mutex);
            QByteArray inner;
            const QByteArray nonce = ScV2::nonce(m_cardCounter);
            if (ScV2::ccmDecrypt(m_keys.hostToCard, nonce, apdu.mid(5, static_cast<uint8_t>(apdu.at(4))), inner)) {
                reply = ScV2::ccmEncrypt(m_keys.cardToHost, nonce, QByteArray::fromHex("9000"))
                        + QByteArray::fromHex("9000");
                ++m_cardCounter;
            }
        }
        --m_inFlight;
        return reply;
    }
    bool isConnected() const override { return true; }

    int maxInFlight() const { return m_maxInFlight.load(); }

    QSemaphore entered;
    QSemaphore release;
    QList<QByteArray> sent;

private:
    ScV2::SessionKeys m_keys;
    QMutex m_mutex;
    quint64 m_cardCounter = 0;
    std::atomic<int> m_inFlight{0};
    std::atomic<int> m_maxInFlight{0};
};

} // anonymous namespace

/**
 * @brief Keycard applet 4.x SecureChannelV2: crypto primitives, SELECT parsing and the
 * CommandSet flows against a card emulator speaking real AES-CCM.
 */
class TestSecureChannelV2 : public QObject {
    Q_OBJECT

private:
    static QByteArray testCaPrivateKey() {
        return QCryptographicHash::hash("keycard-qt unit test CA", QCryptographicHash::Sha256);
    }
    static QByteArray otherCaPrivateKey() {
        return QCryptographicHash::hash("keycard-qt unit test other CA", QCryptographicHash::Sha256);
    }

    int countInstruction(uint8_t ins) const {
        int count = 0;
        for (const QByteArray& apdu : m_mock->getTransmittedApdus()) {
            if (apdu.size() >= 2 && static_cast<uint8_t>(apdu.at(1)) == ins) {
                ++count;
            }
        }
        return count;
    }

    int countInner(uint8_t ins) const {
        int count = 0;
        for (const auto& command : m_card->innerCommands) {
            count += command.ins == ins ? 1 : 0;
        }
        return count;
    }

    const ScV2CardEmulator::InnerCommand* lastInner(uint8_t ins) const {
        for (int i = m_card->innerCommands.size() - 1; i >= 0; --i) {
            if (m_card->innerCommands[i].ins == ins) {
                return &m_card->innerCommands[i];
            }
        }
        return nullptr;
    }

    /// Selected, initialized card with an open, PIN-verified session
    void openInitializedCard() {
        m_card->initialized = true;
        m_card->pin = QStringLiteral("123456");
        m_card->keyUID = QByteArray(32, 0x42);
        m_cmdSet->select(true);
        QVERIFY(m_cmdSet->verifyPIN(QStringLiteral("123456")));
        m_card->innerCommands.clear();
        m_mock->clearTransmittedApdus();
    }

    std::shared_ptr<KeycardChannel> m_channel;
    MockBackend* m_mock = nullptr;
    std::shared_ptr<CommandSet> m_cmdSet;
    std::unique_ptr<ScV2CardEmulator> m_card;

private slots:
    void initTestCase() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(true);
        m_channel = std::make_shared<KeycardChannel>(mock);
        m_mock = mock;
    }

    void init() {
        m_mock->reset();
        m_mock->simulateCardInserted();
        m_card = std::make_unique<ScV2CardEmulator>(testCaPrivateKey());
        ScV2CardEmulator* card = m_card.get();
        m_mock->setResponseHandler([card](const QByteArray& apdu) { return card->handle(apdu); });
        m_cmdSet = std::make_shared<CommandSet>(m_channel, nullptr, nullptr);
        m_cmdSet->setTrustedCaKeys({ m_card->caPublicKey() });
    }

    void cleanup() {
        m_cmdSet.reset();
        m_card.reset();
    }

    // ---- Crypto primitives ----

    void testNonceIsBigEndianCounter() {
        QCOMPARE(ScV2::nonce(0), QByteArray(13, '\0'));
        QCOMPARE(ScV2::nonce(0x0102).toHex(), QByteArray("00000000000000000000000102"));
    }

    void testSessionKeysAreHkdfSha256() {
        // RFC 5869 HKDF with salt = salt, IKM = X, info = "sc_v2_ccm", L = 32
        const QByteArray salt(32, 0x01);
        const QByteArray shared(32, 0x02);
        const QByteArray prk = QMessageAuthenticationCode::hash(shared, salt, QCryptographicHash::Sha256);
        const QByteArray okm = QMessageAuthenticationCode::hash(QByteArray("sc_v2_ccm\x01"), prk,
                                                                QCryptographicHash::Sha256);
        const ScV2::SessionKeys keys = ScV2::deriveSessionKeys(salt, shared);
        QCOMPARE(keys.hostToCard, okm.left(16));
        QCOMPARE(keys.cardToHost, okm.mid(16, 16));
    }

    void testCcmRoundTripAndTamper() {
        const QByteArray key(16, 0x11);
        const QByteArray nonce = ScV2::nonce(7);
        const QByteArray plaintext = QByteArray::fromHex("80F2000000");
        const QByteArray wrapped = ScV2::ccmEncrypt(key, nonce, plaintext);
        QCOMPARE(wrapped.size(), plaintext.size() + ScV2::TagSize);

        QByteArray out;
        QVERIFY(ScV2::ccmDecrypt(key, nonce, wrapped, out));
        QCOMPARE(out, plaintext);

        QByteArray tampered = wrapped;
        tampered[0] = static_cast<char>(tampered[0] ^ 0x01);
        QVERIFY(!ScV2::ccmDecrypt(key, nonce, tampered, out));
        QVERIFY(!ScV2::ccmDecrypt(key, ScV2::nonce(8), wrapped, out));
    }

    void testEcdhIsSymmetric() {
        const ScV2::KeyPair a = ScV2::generateKeyPair();
        const ScV2::KeyPair b = ScV2::generateKeyPair();
        QVERIFY(a.isValid() && b.isValid());
        const QByteArray ab = ScV2::ecdhSharedSecret(a.privateKey, b.publicKey);
        QCOMPARE(ab.size(), 32);
        QCOMPARE(ab, ScV2::ecdhSharedSecret(b.privateKey, a.publicKey));
    }

    void testSignVerifyAndRecover() {
        const ScV2::KeyPair key = ScV2::keyPairFromPrivateKey(testCaPrivateKey());
        QVERIFY(key.isValid());
        const QByteArray digest = QCryptographicHash::hash("message", QCryptographicHash::Sha256);
        const ScV2::RecoverableSignature sig = ScV2::sign(key.privateKey, digest);
        QVERIFY(sig.isValid());

        QVERIFY(ScV2::verifySignature(key.publicKey, digest, sig.der));
        QVERIFY(ScV2::verifySignature(ScV2::compressPublicKey(key.publicKey), digest, sig.der));
        QVERIFY(!ScV2::verifySignature(key.publicKey,
                                       QCryptographicHash::hash("other", QCryptographicHash::Sha256), sig.der));
        QCOMPARE(ScV2::recoverPublicKey(digest, sig.r, sig.s, sig.recId), ScV2::compressPublicKey(key.publicKey));
    }

    void testGeneratorPublicKey() {
        // Private key 1 -> secp256k1 generator
        const ScV2::KeyPair one = ScV2::keyPairFromPrivateKey(QByteArray(31, '\0') + QByteArray(1, '\x01'));
        QCOMPARE(one.publicKey.toHex(),
                 QByteArray("0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
                            "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"));
        QVERIFY(!ScV2::keyPairFromPrivateKey(QByteArray(32, '\0')).isValid());
    }

    void testCertificateTrust() {
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        const QByteArray certData = ScV2::Certificate::create(testCaPrivateKey(), identity.publicKey);
        QCOMPARE(certData.size(), ScV2::CertificateSize);

        const ScV2::Certificate cert = ScV2::Certificate::parse(certData);
        QVERIFY(cert.isValid());
        QCOMPARE(cert.identityPublicKey, ScV2::compressPublicKey(identity.publicKey));

        const QByteArray caCompressed =
            ScV2::compressPublicKey(ScV2::keyPairFromPrivateKey(testCaPrivateKey()).publicKey);
        const QByteArray caUncompressed = ScV2::keyPairFromPrivateKey(testCaPrivateKey()).publicKey;
        const QByteArray otherCa = ScV2::keyPairFromPrivateKey(otherCaPrivateKey()).publicKey;
        QCOMPARE(cert.recoverCaPublicKey(), caCompressed);
        QVERIFY(cert.isTrustedBy({ caCompressed }));
        QVERIFY(cert.isTrustedBy({ otherCa, caUncompressed }));
        QVERIFY(!cert.isTrustedBy({ otherCa }));
        QVERIFY(!cert.isTrustedBy({}));

        // Ethereum-style v (27/28) is accepted too
        QByteArray v27 = certData;
        v27[ScV2::CertificateSize - 1] = static_cast<char>(cert.recId + 27);
        QVERIFY(ScV2::Certificate::parse(v27).isTrustedBy({ caCompressed }));

        // A swapped identity key no longer matches the CA signature
        QByteArray forged = certData;
        forged.replace(0, ScV2::CompressedPublicKeySize, ScV2::compressPublicKey(ScV2::generateKeyPair().publicKey));
        QVERIFY(!ScV2::Certificate::parse(forged).isTrustedBy({ caCompressed }));

        QVERIFY(!ScV2::Certificate::parse(certData.left(97)).isValid());
    }

    // ---- Known answers from the real applet ----
    // status-keycard 4.0.1 in jcardsim driven by status-keycard-java 4.0.0, cross-checked with
    // BouncyCastle (status-keycard-qt test/keycard-simulator/tools/scv2-vectors.json).

    void testKnownAnswerCertificate() {
        const QByteArray caPub = QByteArray::fromHex("036a5b809145f2c8f75d9737affd72d0a01c6696cbc5efdc69db70a3d2fbbf26fa");
        const QByteArray selectResponse = QByteArray::fromHex(
            "a470020204008c01338e008d011f8a6203aa1414de2f991a3ffe699da598cc776617229b999ad78aa0b1bf45bd0c974be7"
            "a8141d0754940e9762fd5e029d25a06026d067cf9182ab4883f5462c0475329b77f84eaa3dab1be05045bcf04d3f37602455"
            "4405dcf6f7a8e0e630d7405c30f700");
        const ApplicationInfo info = parseApplicationInfo(selectResponse);
        QVERIFY(info.usesSecureChannelV2());
        QVERIFY(info.initialized);
        QCOMPARE(info.pinRetries, 3);
        QCOMPARE(info.identityPublicKey.toHex(),
                 QByteArray("03aa1414de2f991a3ffe699da598cc776617229b999ad78aa0b1bf45bd0c974be7"));

        const ScV2::Certificate cert = ScV2::Certificate::parse(info.certificate);
        QCOMPARE(cert.recId, 0);  // raw recovery id, not +27
        QCOMPARE(cert.recoverCaPublicKey(), caPub);
        QVERIFY(cert.isTrustedBy({ caPub }));
    }

    void testKnownAnswerHandshake() {
        const QByteArray salt = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
        const ScV2::KeyPair host = ScV2::keyPairFromPrivateKey(
            QByteArray::fromHex("cffc7c79f5bdff04696f798d3d776412a55033421a6431d59ad34b1392ff6ef3"));
        QCOMPARE(host.publicKey.toHex(),
                 QByteArray("048b6ee1ded30073ce48e49aba030f02848e57711d00d9d4a5d3c68ad896af208131e203ae52be9945fba379"
                            "dfe5528643a5281262b8dd0ce4cd2572768c8f4f48"));
        const QByteArray cardPub = QByteArray::fromHex(
            "0436129a6b84bdb9ae75c8258cb179c7753ca4f9f3699ccbbd061cb1a89f90954177d833eeadc70863cd3ec8bc2576afb77b"
            "0c6fbb9d959c9cacc98b3f815adf36");
        const QByteArray signature = QByteArray::fromHex(
            "3045022100edf0e5b8d6c434e86f4b1e1a8ad7ed0257cb20b9f28e6aeab9fdf31e172f691502203a3bae71e7110650305b77"
            "1d5b31ec9a0d6e1ba446d4f9c183123f87fcd4b4eb");
        const QByteArray identityPub = QByteArray::fromHex("03aa1414de2f991a3ffe699da598cc776617229b999ad78aa0b1bf45bd0c974be7");

        const QByteArray shared = ScV2::ecdhSharedSecret(host.privateKey, cardPub);
        QCOMPARE(shared.toHex(), QByteArray("b2f4f77c5a5cb7c8b3d5b308ef319e2b5085e93e4b41856fe84c9c937814c238"));
        const ScV2::SessionKeys keys = ScV2::deriveSessionKeys(salt, shared);
        QCOMPARE(keys.hostToCard.toHex(), QByteArray("4bf1a373b5128e335b7f4c3efaddfa48"));
        QCOMPARE(keys.cardToHost.toHex(), QByteArray("884291dc15bd1ac1b8797e5e0cd4b6d2"));

        const QByteArray digest = ScV2::transcriptDigest(salt, host.publicKey, cardPub);
        QCOMPARE(digest.toHex(), QByteArray("e1d6e198ff43d0bdba24c3b3c17e055d5a0df07e246727ee099e7dbfda1b6b12"));
        QVERIFY(ScV2::verifySignature(identityPub, digest, signature));
    }

    void testKnownAnswerSecuredApdu() {
        // GET STATUS round trips at nonce 0 and 1: what SecureChannelV2::send puts on the wire
        // (the vectors' outer APDU has no Le; ours appends Le=00) and how it unwraps the reply
        const ScV2::SessionKeys keys{ QByteArray::fromHex("4bf1a373b5128e335b7f4c3efaddfa48"),
                                      QByteArray::fromHex("884291dc15bd1ac1b8797e5e0cd4b6d2") };
        const QByteArray outers[] = { QByteArray::fromHex("801800000d18a840a67754aba0fdd4300201"),
                                      QByteArray::fromHex("801800000de437d845c418f4b2890596eb86") };
        const QByteArray replies[] = { QByteArray::fromHex("6ebf8aef8364bcaa9f2bda25d96afb6325ece495659000"),
                                       QByteArray::fromHex("95100eafd20e8d34970b51b8595e372f950130746a9000") };

        int exchange = 0;
        m_mock->setResponseHandler([&](const QByteArray& apdu) {
            const QByteArray expected = outers[exchange] + QByteArray(1, '\0');
            return apdu == expected ? replies[exchange++] : QByteArray::fromHex("6F00");
        });
        SecureChannelV2 channel(m_channel.get());
        channel.testOpenWithKeys(keys);
        for (int i = 0; i < 2; ++i) {
            const APDU::Response response = channel.send(APDU::Command(APDU::CLA, APDU::INS_GET_STATUS, 0, 0));
            QCOMPARE(response.sw(), uint16_t(0x9000));
            QCOMPARE(response.data().toHex(), QByteArray("a309020103020105010100"));
        }
        QCOMPARE(exchange, 2);
        QCOMPARE(channel.testCounter(), quint64(2));
    }

    // ---- Concurrency ----

    void testOverlappingSendsNeverReuseANonce() {
        const ScV2::SessionKeys keys{ QByteArray(16, 0x11), QByteArray(16, 0x22) };
        BlockingScV2Channel transport(keys);
        SecureChannelV2 channel(&transport);
        channel.testOpenWithKeys(keys);

        uint16_t swA = 0;
        uint16_t swB = 0;
        std::thread first([&]() {
            swA = channel.send(APDU::Command(APDU::CLA, APDU::INS_VERIFY_PIN, 0, 0)).sw();
        });
        QVERIFY(transport.entered.tryAcquire(1, 5000));

        std::thread second([&]() {
            swB = channel.send(APDU::Command(APDU::CLA, APDU::INS_GET_STATUS, 0, 0)).sw();
        });
        // The second exchange must wait for the first one to finish
        QVERIFY(!transport.entered.tryAcquire(1, 200));

        transport.release.release();
        QVERIFY(transport.entered.tryAcquire(1, 5000));
        transport.release.release();
        first.join();
        second.join();

        QCOMPARE(transport.maxInFlight(), 1);
        QCOMPARE(transport.sent.size(), 2);
        // The card accepted both under consecutive nonces
        QCOMPARE(swA, uint16_t(0x9000));
        QCOMPARE(swB, uint16_t(0x9000));
        QCOMPARE(channel.testCounter(), quint64(2));
        QByteArray inner;
        const QByteArray secondPayload = transport.sent[1].mid(5, static_cast<uint8_t>(transport.sent[1].at(4)));
        QVERIFY(!ScV2::ccmDecrypt(keys.hostToCard, ScV2::nonce(0), secondPayload, inner));
        QVERIFY(ScV2::ccmDecrypt(keys.hostToCard, ScV2::nonce(1), secondPayload, inner));
    }

    void testResetIsNotBlockedByAnInFlightExchange() {
        const ScV2::SessionKeys keys{ QByteArray(16, 0x11), QByteArray(16, 0x22) };
        BlockingScV2Channel transport(keys);
        SecureChannelV2 channel(&transport);
        channel.testOpenWithKeys(keys);

        bool threw = false;
        std::thread sender([&]() {
            try {
                channel.send(APDU::Command(APDU::CLA, APDU::INS_GET_STATUS, 0, 0));
            } catch (const std::runtime_error&) {
                threw = true;
            }
        });
        QVERIFY(transport.entered.tryAcquire(1, 5000));

        // Card loss on the channel thread while the exchange is in flight
        channel.reset();
        QVERIFY(!channel.isOpen());

        transport.release.release();
        sender.join();
        QVERIFY2(threw, "the reply of a reset session must be rejected");
    }

    // ---- Applet 3.x identity (IDENTIFY) ----

    /// An initialized applet 3.x card whose IDENTIFY answers with @p certificate, signing the challenge
    /// with @p identityPrivateKey; an empty certificate answers 6985 (no certificate)
    void installV1Card(const QByteArray& certificate, const QByteArray& identityPrivateKey, int* identifyCount,
                       bool initialized = true) {
        const QByteArray scKey = ScV2::generateKeyPair().publicKey;
        m_mock->setResponseHandler([=](const QByteArray& apdu) {
            const uint8_t ins = static_cast<uint8_t>(apdu.at(1));
            if (ins == APDU::INS_SELECT) {
                if (!initialized) {
                    return QByteArray::fromHex("8041") + scKey + QByteArray::fromHex("9000");
                }
                const QByteArray body = QByteArray::fromHex("8f10") + QByteArray(16, 0x11) + QByteArray::fromHex("8041")
                    + scKey + QByteArray::fromHex("02020302020103" "8d013f");
                return QByteArray::fromHex("a4") + QByteArray(1, char(body.size())) + body + QByteArray::fromHex("9000");
            }
            if (ins == APDU::INS_IDENTIFY) {
                ++*identifyCount;
                if (certificate.isEmpty()) {
                    return QByteArray::fromHex("6985");
                }
                const QByteArray challenge = apdu.mid(5, static_cast<uint8_t>(apdu.at(4)));
                const QByteArray inner = QByteArray::fromHex("8a62") + certificate
                    + ScV2::sign(identityPrivateKey, challenge).der;
                return QByteArray::fromHex("a081") + QByteArray(1, char(inner.size())) + inner + QByteArray::fromHex("9000");
            }
            return QByteArray::fromHex("9000");
        });
    }

    void testV1CardIdentifiedByTrustedCa() {
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        int identifies = 0;
        installV1Card(ScV2::Certificate::create(testCaPrivateKey(), identity.publicKey), identity.privateKey, &identifies);

        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->applicationInfo().usesSecureChannelV2());
        QVERIFY(m_cmdSet->isCardTrusted());
        QCOMPARE(m_cmdSet->applicationInfo().identityPublicKey, ScV2::compressPublicKey(identity.publicKey));
        QCOMPARE(m_cmdSet->applicationInfo().instanceUID, QByteArray(16, 0x11));  // 3.x keeps its own UID
        QCOMPARE(identifies, 1);

        // Identified once per card: SELECT runs often
        m_cmdSet->select(true);
        QVERIFY(m_cmdSet->isCardTrusted());
        QCOMPARE(identifies, 1);
    }

    void testV1ReplacementWithSameUidIsIdentifiedAgain() {
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        int identifies = 0;
        installV1Card(ScV2::Certificate::create(testCaPrivateKey(), identity.publicKey), identity.privateKey, &identifies);
        m_cmdSet->select(true);
        QVERIFY(m_cmdSet->isCardTrusted());

        // The SELECT UID is unauthenticated: a replacement claiming it must not inherit the verified identity
        m_mock->simulateCardRemoved();
        QCoreApplication::processEvents();
        int replacementIdentifies = 0;
        installV1Card(QByteArray(), QByteArray(), &replacementIdentifies);
        m_mock->simulateCardInserted();
        QCoreApplication::processEvents();

        m_cmdSet->select(true);
        QCOMPARE(m_cmdSet->applicationInfo().instanceUID, QByteArray(16, 0x11));
        QCOMPARE(replacementIdentifies, 1);
        QVERIFY(!m_cmdSet->isCardTrusted());
        QVERIFY(m_cmdSet->applicationInfo().certificate.isEmpty());
    }

    void testV1CardFromUnknownCa() {
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        int identifies = 0;
        installV1Card(ScV2::Certificate::create(otherCaPrivateKey(), identity.publicKey), identity.privateKey, &identifies);
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->isCardTrusted());
        QCOMPARE(identifies, 1);
    }

    void testV1CardWithoutCertificate() {
        int identifies = 0;
        installV1Card(QByteArray(), QByteArray(), &identifies);
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->isCardTrusted());
        QVERIFY(m_cmdSet->applicationInfo().certificate.isEmpty());
        // IDENTIFY's 6985 is advisory and must not surface as the caller's error
        QVERIFY2(m_cmdSet->lastError().isEmpty(), qPrintable(m_cmdSet->lastError()));
    }

    void testV1CardSigningWithAnotherKey() {
        // The certificate is genuine, but the card does not hold its identity key
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        int identifies = 0;
        installV1Card(ScV2::Certificate::create(testCaPrivateKey(), identity.publicKey),
                      ScV2::generateKeyPair().privateKey, &identifies);
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->isCardTrusted());
        QVERIFY(m_cmdSet->applicationInfo().certificate.isEmpty());
    }

    void testBlankV1CardIsIdentified() {
        // 3.x answers IDENTIFY before INIT too, so a blank card's CA is known up front, as on 4.x
        const ScV2::KeyPair identity = ScV2::generateKeyPair();
        int identifies = 0;
        installV1Card(ScV2::Certificate::create(otherCaPrivateKey(), identity.publicKey), identity.privateKey,
                      &identifies, false);
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->applicationInfo().initialized);
        QVERIFY(!m_cmdSet->isCardTrusted());
        QCOMPARE(m_cmdSet->applicationInfo().identityPublicKey, ScV2::compressPublicKey(identity.publicKey));
        QCOMPARE(identifies, 1);

        // No instance UID to cache by: another blank card may be behind the next SELECT
        m_cmdSet->select(true);
        QCOMPARE(identifies, 2);
    }

    // ---- SELECT parsing ----

    void testParseBlankSelect() {
        const QByteArray response = m_card->selectResponse();
        const ApplicationInfo info = parseApplicationInfo(response.left(response.size() - 2));
        QVERIFY(info.usesSecureChannelV2());
        QCOMPARE(info.secureChannelVersion, uint8_t(2));
        QCOMPARE(info.appVersion, uint8_t(4));
        QCOMPARE(info.appVersionMinor, uint8_t(0));
        QVERIFY(!info.initialized);
        QCOMPARE(info.pinRetries, -1);
        QVERIFY(info.keyUID.isEmpty());
        QVERIFY(info.secureChannelPublicKey.isEmpty());
        QCOMPARE(info.availableSlots, uint8_t(0));
        QCOMPARE(info.capabilities, uint8_t(0x1F));
        QCOMPARE(info.certificate, m_card->certificate());
        QCOMPARE(info.identityPublicKey, m_card->identityPublicKey());
        QCOMPARE(info.instanceUID,
                 QCryptographicHash::hash(m_card->identityPublicKey(), QCryptographicHash::Sha256).left(16));
    }

    void testParseInitializedSelectWithLongLength() {
        m_card->initialized = true;
        m_card->pinRetries = 2;
        m_card->keyUID = QByteArray(32, 0x42);
        const QByteArray response = m_card->selectResponse();
        QCOMPARE(static_cast<uint8_t>(response.at(1)), uint8_t(0x81));  // template > 127 bytes
        const ApplicationInfo info = parseApplicationInfo(response.left(response.size() - 2));
        QVERIFY(info.initialized);
        QCOMPARE(info.pinRetries, 2);
        QCOMPARE(info.keyUID, QByteArray(32, 0x42));
        QCOMPARE(info.certificate.size(), ScV2::CertificateSize);
    }

    void testParseSelectWithShortTemplateLength() {
        // A real applet 4.0 card (before 4.0.1, status-keycard #133): the template says 0x6f
        // bytes but carries 0x70, so the certificate's last byte lies past the declared end
        const QByteArray response = QByteArray::fromHex(
            "a46f020204008c01008e008d011f8a62023be4f2475e9feee93f5afdb8e1e2f0b83be5e1845ac2e5d08ec6f5e7c74d3"
            "61e99fe9ff3ca18d63e51a27b673cc73c0c22efbb30975e5d00e229040291b5c45b4cb9c669dae88f15fbfaaa3d2f63"
            "543479673c4d3636f7584996d7d2c2e189e100");
        const ApplicationInfo info = parseApplicationInfo(response);
        QVERIFY(info.usesSecureChannelV2());
        QVERIFY(!info.initialized);
        QCOMPARE(info.certificate.size(), ScV2::CertificateSize);
        QCOMPARE(info.instanceUID.size(), 16);

        const ScV2::Certificate cert = ScV2::Certificate::parse(info.certificate);
        QCOMPARE(cert.recoverCaPublicKey().toHex(),
                 QByteArray("025877220aaae6e54a6f974602d5995c0fe24a3ea7ddabd8644bec795b9da00743"));

        // The same bytes with a correct length parse identically
        QByteArray fixed = response;
        fixed[1] = static_cast<char>(0x70);
        QCOMPARE(parseApplicationInfo(fixed).certificate, info.certificate);
    }

    void testParseV1SelectUnchanged() {
        const QByteArray pubKey = ScV2::generateKeyPair().publicKey;
        const QByteArray body = QByteArray::fromHex("8f10") + QByteArray(16, 0x11) + QByteArray::fromHex("8041")
            + pubKey + QByteArray::fromHex("0202030202010a8d013f");
        const ApplicationInfo info = parseApplicationInfo(QByteArray(1, '\xA4') + QByteArray(1, char(body.size())) + body);
        QVERIFY(!info.usesSecureChannelV2());
        QCOMPARE(info.secureChannelVersion, uint8_t(1));
        QCOMPARE(info.instanceUID, QByteArray(16, 0x11));
        QCOMPARE(info.availableSlots, uint8_t(10));
        QCOMPARE(info.secureChannelPublicKey, pubKey);

        const ApplicationInfo preInit = parseApplicationInfo(QByteArray::fromHex("8041") + pubKey);
        QVERIFY(!preInit.initialized);
        QCOMPARE(preInit.secureChannelPublicKey, pubKey);
    }

    // ---- CommandSet against the emulator ----

    void testUnknownCaCardIsUsable() {
        // Development or custom-made cards: reported, never refused
        m_cmdSet->setTrustedCaKeys({ ScV2::keyPairFromPrivateKey(otherCaPrivateKey()).publicKey });
        openInitializedCard();
        QVERIFY(!m_cmdSet->isCardTrusted());
        QVERIFY(!m_cmdSet->applicationInfo().certificateTrusted);
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());
        QVERIFY(m_cmdSet->getStatus().valid);
    }

    void testTrustedCaCardIsReportedTrusted() {
        m_cmdSet->select(true);
        QVERIFY(m_cmdSet->isCardTrusted());
        QVERIFY(m_cmdSet->applicationInfo().certificateTrusted);
    }

    void testDefaultTrustRejectsUnknownCa() {
        m_cmdSet->setTrustedCaKeys(defaultTrustedCaKeys());
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->isCardTrusted());
    }

    void testWrongTranscriptSignatureFailsAuthentication() {
        m_card->initialized = true;
        m_card->signWrongTranscript = true;
        m_cmdSet->select(true);
        QVERIFY(m_cmdSet->isCardTrusted());
        QVERIFY(!m_cmdSet->ensureSecureChannel());
        QCOMPARE(m_cmdSet->lastError(), QString::fromLatin1(Errors::CardAuthenticationFailed));
        QVERIFY(!m_cmdSet->testSecureChannelIsOpen());
    }

    void testInitBlankCardWithoutPairing() {
        const Secrets secrets(QStringLiteral("123456"), QStringLiteral("123456789012"), QStringLiteral("ignored"));
        QVERIFY2(m_cmdSet->init(secrets), qPrintable(m_cmdSet->lastError()));

        QVERIFY(m_card->initialized);
        QCOMPARE(m_card->pin, QStringLiteral("123456"));
        QCOMPARE(m_card->puk, QStringLiteral("123456789012"));
        const auto* init = lastInner(APDU::INS_INIT);
        QVERIFY(init);
        QCOMPARE(init->data, QByteArray("123456123456789012"));  // no pairing secret

        QCOMPARE(countInstruction(APDU::INS_PAIR), 0);
        QCOMPARE(countInstruction(APDU::INS_MUTUALLY_AUTHENTICATE), 0);
        QCOMPARE(countInstruction(APDU::INS_INIT), 0);  // never in plaintext
        QVERIFY(m_cmdSet->applicationInfo().initialized);
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());
        QVERIFY(m_cmdSet->testWasAuthenticated());
        QVERIFY(m_cmdSet->hasCachedStatus());
        QCOMPARE(m_cmdSet->cachedApplicationStatus().pinRetryCount, uint8_t(3));
    }

    void testCommandsRunOverCcmAndAdvanceTheNonce() {
        openInitializedCard();
        const quint64 before = m_card->counter();
        const ApplicationStatus status = m_cmdSet->getStatus();
        QVERIFY(status.valid);
        QCOMPARE(status.pukRetryCount, uint8_t(5));
        QVERIFY(status.keyInitialized);
        QCOMPARE(m_card->counter(), before + 1);
        QCOMPARE(countInstruction(APDU::INS_SECURED_APDU), 1);
        QCOMPARE(countInstruction(APDU::INS_GET_STATUS), 0);  // only wrapped
    }

    void testWrongPinReportsRetries() {
        openInitializedCard();
        QVERIFY(!m_cmdSet->verifyPIN(QStringLiteral("000000")));
        QCOMPARE(m_cmdSet->remainingPINAttempts(), 2);
    }

    void testCardDroppedSessionIsReopened() {
        openInitializedCard();
        const int opensBefore = m_card->openCount;
        m_card->rejectNextSecured = true;
        QVERIFY(!m_cmdSet->getStatus().valid);  // plaintext 6982
        QVERIFY(!m_cmdSet->testSecureChannelIsOpen());

        // Next command opens a new session and re-verifies the cached PIN
        QVERIFY(m_cmdSet->getStatus().valid);
        QCOMPARE(m_card->openCount, opensBefore + 1);
        QVERIFY(countInner(APDU::INS_VERIFY_PIN) >= 1);
    }

    void testOversizedCommandIsRejectedBeforeTransmit() {
        openInitializedCard();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, m_cmdSet->storeData(0x00, QByteArray(300, 0x01)));
        QCOMPARE(countInstruction(APDU::INS_SECURED_APDU), 0);
    }

    void testPairingOnlyCommandsAreRejected() {
        openInitializedCard();
        QVERIFY(!m_cmdSet->pair(QStringLiteral("pw")).isValid());
        QVERIFY(!m_cmdSet->unpair(0));
        QVERIFY(!m_cmdSet->mutualAuthenticate());
        QVERIFY(!m_cmdSet->changePairingSecret(QStringLiteral("pw")));
        QVERIFY(m_cmdSet->identify().isEmpty());
        QVERIFY(!m_cmdSet->deriveKey(QStringLiteral("m/44'/60'/0'/0/0")));
        QVERIFY(!m_cmdSet->setPinlessPath(QStringLiteral("m/44'/60'/0'/0/0")));
        QVERIFY(m_cmdSet->signPinless(QByteArray(32, 0x01)).isEmpty());
        QVERIFY(m_cmdSet->sign(QByteArray(32, 0x01)).isEmpty());
        QVERIFY(!m_cmdSet->getStatus(APDU::P1GetStatusKeyPath).valid);
        QVERIFY(m_cmdSet->lastError().contains(QStringLiteral("not supported by Keycard applet 4.0")));

        QVERIFY(m_card->innerCommands.isEmpty());
        QCOMPARE(m_mock->getTransmittedApdus().size(), 0);
    }

    void testSignAlwaysDerivesFromMaster() {
        openInitializedCard();
        QVERIFY(!m_cmdSet->signWithPathFullResponse(QByteArray(32, 0x01), QStringLiteral("m/44'/60'/0'/0/0"), true)
                     .isEmpty());
        const auto* sign = lastInner(APDU::INS_SIGN);
        QVERIFY(sign);
        QCOMPARE(sign->p1, APDU::P1SignDerive);
        QCOMPARE(sign->p2, APDU::P2SignECDSA);
        QCOMPARE(sign->data.size(), 32 + 5 * 4);

        QVERIFY(m_cmdSet->signWithPath(QByteArray(32, 0x01), QStringLiteral("../0")).isEmpty());
        QCOMPARE(countInner(APDU::INS_SIGN), 1);
    }

    void testExportKeyPathsAreAbsolute() {
        openInitializedCard();
        QVERIFY(!m_cmdSet->exportKey(true, true, QStringLiteral("m/43'/60'/1581'/0'/0"),
                                     APDU::P2ExportKeyPrivateAndPublic).isEmpty());
        const auto* exported = lastInner(APDU::INS_EXPORT_KEY);
        QVERIFY(exported);
        QCOMPARE(exported->p1, uint8_t(0x00));
        QCOMPARE(exported->p2, APDU::P2ExportKeyPrivateAndPublic);
        QCOMPARE(exported->data.size(), 5 * 4);

        // Without derivation: the master key (empty path)
        QVERIFY(!m_cmdSet->exportKey(false, false, QString(), APDU::P2ExportKeyPublicOnly).isEmpty());
        QVERIFY(lastInner(APDU::INS_EXPORT_KEY)->data.isEmpty());

        // The master extended public key cannot be exported on 4.x
        QVERIFY(m_cmdSet->exportKeyExtended(false, false, QString()).isEmpty());
        QVERIFY(m_cmdSet->exportKeyExtended(true, false, QStringLiteral("m")).isEmpty());
        QCOMPARE(countInner(APDU::INS_EXPORT_KEY), 2);

        QVERIFY(!m_cmdSet->exportKeyExtended(true, false, QStringLiteral("m/44'/60'/0'/0")).isEmpty());
        QCOMPARE(lastInner(APDU::INS_EXPORT_KEY)->p2, APDU::P2ExportKeyExtendedPublic);
    }

    void testSelectClosesTheSession() {
        openInitializedCard();
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());
        m_cmdSet->select(true);
        QVERIFY(!m_cmdSet->testSecureChannelIsOpen());
        QVERIFY(m_cmdSet->getStatus().valid);  // transparently reopened
    }

    void testInstanceUidIsStablePerCard() {
        m_cmdSet->select(true);
        const QByteArray first = m_cmdSet->applicationInfo().instanceUID;
        m_card->initialized = true;
        m_cmdSet->select(true);
        QCOMPARE(m_cmdSet->applicationInfo().instanceUID, first);

        ScV2CardEmulator other(testCaPrivateKey());
        const QByteArray otherResponse = other.selectResponse();
        QVERIFY(parseApplicationInfo(otherResponse.left(otherResponse.size() - 2)).instanceUID != first);
    }
};

QTEST_MAIN(TestSecureChannelV2)
#include "test_secure_channel_v2.moc"
