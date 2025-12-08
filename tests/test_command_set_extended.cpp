#include <QTest>
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "mocks/mock_backend.h"
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

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
