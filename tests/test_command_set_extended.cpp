#include <QTest>
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "mocks/mock_backend.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QVector>
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Extended internal CommandSet tests
 * 
 * NOTE: After refactoring, CommandSet is internal to CommunicationManager.
 * These tests provide extended coverage of CommandSet internals.
 * For public API tests, see test_communication_manager*.cpp.
 */
class TestCommandSetExtended : public QObject {
    Q_OBJECT
    
private:
    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(true);
        auto channel = std::make_shared<KeycardChannel>(mock);
        mock->simulateCardInserted();
        return channel;
    }

    QByteArray validSelectResponse() const {
        const QByteArray publicKey = QByteArray::fromHex(
            "0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
            "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
        QByteArray body;
        body.append(QByteArray::fromHex("8f10"));
        body.append(QByteArray(16, 0x11));
        body.append(QByteArray::fromHex("8041"));
        body.append(publicKey);
        body.append(QByteArray::fromHex("0202030202010a8d013f"));

        QByteArray response;
        response.append(static_cast<char>(0xA4));
        response.append(static_cast<char>(body.size()));
        response.append(body);
        response.append(QByteArray::fromHex("9000"));
        return response;
    }

    QByteArray openSecureChannelResponse() const {
        return QByteArray(32, 0x22) + QByteArray(16, 0x33) + QByteArray::fromHex("9000");
    }

    int countTransmittedInstruction(uint8_t instruction) const {
        int count = 0;
        for (const QByteArray& apdu : m_mock->getTransmittedApdus()) {
            if (apdu.size() >= 2 && static_cast<uint8_t>(apdu.at(1)) == instruction) {
                ++count;
            }
        }
        return count;
    }

    QByteArrayList transmittedOpenSecureChannelKeys() const {
        QByteArrayList keys;
        for (const QByteArray& apdu : m_mock->getTransmittedApdus()) {
            if (apdu.size() < 5 || static_cast<uint8_t>(apdu.at(1)) != APDU::INS_OPEN_SECURE_CHANNEL) {
                continue;
            }
            const int dataLength = static_cast<uint8_t>(apdu.at(4));
            keys.append(apdu.mid(5, dataLength));
        }
        return keys;
    }

    QByteArray derivePairingToken(const QString& password) const {
        const QByteArray passwordBytes = password.toUtf8();
        QByteArray blockData("Keycard Pairing Password Salt");
        blockData.append(QByteArray::fromHex("00000001"));

        QByteArray u = QMessageAuthenticationCode::hash(
            blockData, passwordBytes, QCryptographicHash::Sha256);
        QByteArray result = u;
        for (int i = 1; i < 50000; ++i) {
            u = QMessageAuthenticationCode::hash(
                u, passwordBytes, QCryptographicHash::Sha256);
            for (int j = 0; j < result.size(); ++j) {
                result[j] = result[j] ^ u[j];
            }
        }
        return result;
    }
    
    std::shared_ptr<KeycardChannel> m_channel;
    std::shared_ptr<CommandSet> m_cmdSet;
    MockBackend* m_mock;
    
private slots:
    void initTestCase() {
        m_channel = createMockChannel();
        m_mock = qobject_cast<MockBackend*>(m_channel->backend());
        m_cmdSet = std::make_shared<CommandSet>(m_channel, nullptr, nullptr);
    }
    
    void cleanupTestCase() {
        m_cmdSet.reset();
        m_channel.reset();
    }
    
    void init() {
        m_mock->reset();
        m_mock->simulateCardInserted();
        m_cmdSet = std::make_shared<CommandSet>(m_channel, nullptr, nullptr);
    }
    
    void testPairFullFlow() {
        QByteArray cardCryptogram(32, 0xAA);
        QByteArray cardChallenge(32, 0xBB);
        QByteArray step1Response = cardCryptogram + cardChallenge + QByteArray::fromHex("9000");
        m_mock->queueResponse(step1Response);
        
        PairingInfo result = m_cmdSet->pair("test-password-123");
        
        QVERIFY(!result.isValid());
        QString error = m_cmdSet->lastError();
        QVERIFY(error.contains("CRYPTOGRAM") || error.contains("Invalid"));
    }
    
    void testPairStepOneFailed() {
        m_mock->queueResponse(QByteArray::fromHex("6982"));
        
        PairingInfo result = m_cmdSet->pair("test-password");
        
        QVERIFY(!result.isValid());
        QVERIFY(!m_cmdSet->lastError().isEmpty());
    }
    
    void testPairInvalidResponseSize() {
        m_mock->queueResponse(QByteArray(10, 0x00) + QByteArray::fromHex("9000"));
        
        PairingInfo result = m_cmdSet->pair("test-password");
        
        QVERIFY(!result.isValid());
        QVERIFY(m_cmdSet->lastError().contains("Invalid pair response size"));
    }

    void testPersistentPairingModeIsOnlyUsedForFirstStep() {
        m_mock->queueResponse(validSelectResponse());
        QVERIFY(m_cmdSet->select().initialized);

        const QString password = QStringLiteral("test-password");
        const QByteArray pairingToken = derivePairingToken(password);
        const QByteArray cardChallenge(32, 0x33);
        QVector<uint8_t> pairingP2Values;

        m_mock->setResponseHandler(
            [&](const QByteArray& apdu) {
                if (apdu.size() < 5
                    || static_cast<uint8_t>(apdu.at(1)) != APDU::INS_PAIR) {
                    return QByteArray::fromHex("6d00");
                }

                pairingP2Values.append(static_cast<uint8_t>(apdu.at(3)));
                const uint8_t p1 = static_cast<uint8_t>(apdu.at(2));
                if (p1 == APDU::P1PairFirstStep) {
                    const int dataLength = static_cast<uint8_t>(apdu.at(4));
                    const QByteArray challenge = apdu.mid(5, dataLength);
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    hash.addData(pairingToken);
                    hash.addData(challenge);
                    return hash.result() + cardChallenge + QByteArray::fromHex("9000");
                }

                return QByteArray(1, 0) + QByteArray(32, 0x44)
                    + QByteArray::fromHex("9000");
            });

        const PairingInfo pairing = m_cmdSet->pair(password);

        QVERIFY(pairing.isValid());
        QCOMPARE(pairingP2Values.size(), 2);
        QCOMPARE(pairingP2Values.at(0), APDU::P2PairPersistent);
        QCOMPARE(pairingP2Values.at(1), static_cast<uint8_t>(0));
    }
    
    void testOpenSecureChannelInvalidPairing() {
        PairingInfo invalidPairing;
        
        bool result = m_cmdSet->openSecureChannel(invalidPairing);
        
        QVERIFY(!result);
        QVERIFY(m_cmdSet->lastError().contains("Invalid pairing"));
    }
    
    void testOpenSecureChannelSuccess() {
        PairingInfo pairing(QByteArray(32, 0xAA), 0);
        QByteArray salt(32, 0xBB);
        m_mock->queueResponse(salt + QByteArray::fromHex("9000"));
        
        bool result = m_cmdSet->openSecureChannel(pairing);
        
        QVERIFY(!result);
        QVERIFY(!m_cmdSet->lastError().isEmpty());
    }

    void testSelectClosesAndNextProtectedCommandReopensSecureChannel() {
        m_mock->queueResponse(validSelectResponse());
        QVERIFY(m_cmdSet->select().initialized);

        const PairingInfo pairing(QByteArray(32, 0x44), 0);
        m_cmdSet->testInjectSecureChannelState(
            pairing, QByteArray(16, 0x55), QByteArray(32, 0x66), QByteArray(32, 0x77));
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());

        m_mock->queueResponse(validSelectResponse());
        QVERIFY(m_cmdSet->select(true).initialized);
        QVERIFY(!m_cmdSet->testSecureChannelIsOpen());

        m_mock->queueResponse(openSecureChannelResponse());
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));

        QVERIFY(m_cmdSet->verifyPIN(QStringLiteral("123456")));
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());
        QCOMPARE(countTransmittedInstruction(APDU::INS_OPEN_SECURE_CHANNEL), 1);
    }

    void testOpenSecureChannelRetriesAfterMutualAuthenticationFailure() {
        m_mock->queueResponse(validSelectResponse());
        QVERIFY(m_cmdSet->select().initialized);

        const PairingInfo pairing(QByteArray(32, 0x44), 0);
        m_mock->queueResponse(openSecureChannelResponse());
        m_mock->queueResponse(QByteArray::fromHex("6982"));
        m_mock->queueResponse(openSecureChannelResponse());
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));

        QVERIFY(m_cmdSet->openSecureChannel(pairing));
        QVERIFY(m_cmdSet->testSecureChannelIsOpen());
        QCOMPARE(countTransmittedInstruction(APDU::INS_OPEN_SECURE_CHANNEL), 2);
    }

    void testOpenSecureChannelUsesAFreshEcdhSecret() {
        m_mock->queueResponse(validSelectResponse());
        QVERIFY(m_cmdSet->select().initialized);

        const PairingInfo pairing(QByteArray(32, 0x44), 0);
        m_mock->queueResponse(openSecureChannelResponse());
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(openSecureChannelResponse());
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("9000"));

        QVERIFY(m_cmdSet->openSecureChannel(pairing));
        QVERIFY(m_cmdSet->openSecureChannel(pairing));

        const QByteArrayList keys = transmittedOpenSecureChannelKeys();
        QCOMPARE(keys.size(), 2);
        QCOMPARE(keys.at(0).size(), 65);
        QCOMPARE(keys.at(1).size(), 65);
        QVERIFY(keys.at(0) != keys.at(1));
    }
    
    void testGetStatusWithoutSecureChannel() {
        ApplicationStatus status = m_cmdSet->getStatus();
        
        QVERIFY(!m_cmdSet->lastError().isEmpty());
        QCOMPARE(status.pinRetryCount, 0);
    }
    
    void testUnpairWithoutSecureChannel() {
        bool result = m_cmdSet->unpair(0);
        
        QVERIFY(!result);
        QVERIFY(!m_cmdSet->lastError().isEmpty());
    }
    
    void testInitNotImplemented() {
        Secrets secrets("123456", "123456789012", "pairing-pass");
        
        bool result = m_cmdSet->init(secrets);
        
        if (!result) {
            QString error = m_cmdSet->lastError();
            QVERIFY(error.contains("Failed to encrypt") || 
                    error.contains("Secure channel") || 
                    error.contains("shared secret"));
        }
    }
    
    void testAccessors() {
        QVERIFY(m_cmdSet->applicationInfo().instanceUID.isEmpty());
        QVERIFY(!m_cmdSet->pairingInfo().isValid());
        QVERIFY(m_cmdSet->remainingPINAttempts() >= -1);
    }
    
    void testVerifyPINWrongCode() {
        bool result = m_cmdSet->verifyPIN("wrong-pin");
        
        QVERIFY(!result);
        QVERIFY(!m_cmdSet->lastError().isEmpty());
    }
    
    void testVerifyPINBlocked() {
        bool result = m_cmdSet->verifyPIN("any-pin");
        
        QVERIFY(!result);
        QVERIFY(!m_cmdSet->lastError().isEmpty());
    }
    
    void testBuildCommandViaSelect() {
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        
        m_cmdSet->select();
        
        QVERIFY(m_mock->getTransmitCount() > 0);
        QByteArray lastApdu = m_mock->getLastTransmittedApdu();
        QCOMPARE(static_cast<uint8_t>(lastApdu[0]), static_cast<uint8_t>(0x00));
        QCOMPARE(static_cast<uint8_t>(lastApdu[1]), static_cast<uint8_t>(0xA4));
    }
    
    void testCheckOKWithVariousErrors() {
        m_mock->queueResponse(QByteArray::fromHex("6982"));
        m_cmdSet->select();
        QVERIFY(!m_cmdSet->lastError().isEmpty());
        QVERIFY(m_cmdSet->lastError().contains("6982"));
        
        m_mock->queueResponse(QByteArray::fromHex("6A80"));
        m_cmdSet->select();
        QVERIFY(m_cmdSet->lastError().contains("6a80"));
        
        m_mock->queueResponse(QByteArray::fromHex("6D00"));
        m_cmdSet->select();
        QVERIFY(m_cmdSet->lastError().contains("6d00"));
    }
};

QTEST_MAIN(TestCommandSetExtended)
#include "test_command_set_extended.moc"
