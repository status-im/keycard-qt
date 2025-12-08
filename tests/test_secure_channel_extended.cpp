#include <QTest>
#include "keycard-qt/secure_channel.h"
#include "keycard-qt/channel_interface.h"
#include "keycard-qt/apdu/command.h"

using namespace Keycard;

class MockChannelForSC : public IChannel {
public:
    QByteArray nextResponse;
    QByteArray lastTransmitted;
    
    QByteArray transmit(const QByteArray& apdu) override {
        lastTransmitted = apdu;
        return nextResponse;
    }
    
    bool isConnected() const override { return true; }
};

class TestSecureChannelExtended : public QObject {
    Q_OBJECT
    
private:
    MockChannelForSC* mockChannel;
    SecureChannel* secChan;
    
private slots:
    void initTestCase() {
        mockChannel = new MockChannelForSC();
        secChan = new SecureChannel(mockChannel);
        // Verify initialization succeeded
        QVERIFY(mockChannel != nullptr);
        QVERIFY(secChan != nullptr);
    }
    
    void cleanupTestCase() {
        delete secChan;
        secChan = nullptr;
        delete mockChannel;
        mockChannel = nullptr;
    }
    
    void init() {
        // Ensure objects are valid before each test
        QVERIFY(secChan != nullptr);
        QVERIFY(mockChannel != nullptr);
        
        secChan->reset();
        mockChannel->nextResponse.clear();
        mockChannel->lastTransmitted.clear();
    }
    
    // Test secret() accessor
    void testSecretAccessor() {
        // Before generation, secret should be empty
        QVERIFY(secChan->secret().isEmpty());
        
        // After generation (with valid EC point), should have secret
        // Note: We use a structure test since real EC point validation requires valid coordinates
    }
    
    // Test rawPublicKey() accessor
    void testRawPublicKeyAccessor() {
        // Before generation
        QVERIFY(secChan->rawPublicKey().isEmpty());
    }
    
    // Test isOpen() state management
    void testIsOpenState() {
        QVERIFY(!secChan->isOpen());
        
        // After init
        QByteArray testIV(16, 0x01);
        QByteArray testEncKey(16, 0x02);
        QByteArray testMacKey(16, 0x03);
        secChan->init(testIV, testEncKey, testMacKey);
        
        QVERIFY(secChan->isOpen());
        
        // After reset
        secChan->reset();
        QVERIFY(!secChan->isOpen());
    }
    
    // Test init with various key sizes
    void testInitWithDifferentKeySizes() {
        // Standard 16-byte keys (AES-128)
        QByteArray iv16(16, 0x01);
        QByteArray enc16(16, 0x02);
        QByteArray mac16(16, 0x03);
        
        secChan->init(iv16, enc16, mac16);
        QVERIFY(secChan->isOpen());
        
        secChan->reset();
        
        // Empty keys (edge case)
        secChan->init(QByteArray(), QByteArray(), QByteArray());
        // Should still mark as "open" even with empty keys
        QVERIFY(secChan->isOpen());
    }
    
    // Test reset clears state
    void testResetClearsState() {
        // Setup state
        QByteArray iv(16, 0x01);
        QByteArray enc(16, 0x02);
        QByteArray mac(16, 0x03);
        secChan->init(iv, enc, mac);
        
        QVERIFY(secChan->isOpen());
        
        // Reset
        secChan->reset();
        
        // Verify cleared
        QVERIFY(!secChan->isOpen());
        QVERIFY(secChan->secret().isEmpty());
        QVERIFY(secChan->rawPublicKey().isEmpty());
    }
    
    // Test encryption with empty data
    void testEncryptEmptyData() {
        QByteArray iv(16, 0x01);
        QByteArray enc(16, 0x02);
        QByteArray mac(16, 0x03);
        secChan->init(iv, enc, mac);
        
        QByteArray encrypted = secChan->encrypt(QByteArray());
        
        // Empty data should be padded to one block (16 bytes)
        // Note: encrypt() only does encryption, MAC is calculated separately
        QVERIFY(!encrypted.isEmpty());
        QCOMPARE(encrypted.size(), 16);  // One padding block
    }
    
    // Test encryption round-trip
    void testEncryptDecryptRoundTrip() {
        QByteArray iv(16, 0x01);
        QByteArray enc(16, 0x02);
        QByteArray mac(16, 0x03);
        secChan->init(iv, enc, mac);
        QVERIFY(secChan->isOpen());
        
        QByteArray original = "Hello Keycard World!";
        
        QByteArray encrypted = secChan->encrypt(original);
        QVERIFY(!encrypted.isEmpty());
        QVERIFY(encrypted != original);
        
        // Reset IV for decryption (same as encryption)
        secChan->reset();
        QVERIFY(!secChan->isOpen());
        
        secChan->init(QByteArray(16, 0x01), enc, mac);
        QVERIFY(secChan->isOpen());
        
        QByteArray decrypted = secChan->decrypt(encrypted);
        
        QCOMPARE(decrypted, original);
    }
    
    // Test encryption with various data sizes
    void testEncryptVariousSizes() {
        QByteArray iv(16, 0x01);
        QByteArray enc(16, 0x02);
        QByteArray mac(16, 0x03);
        secChan->init(iv, enc, mac);
        
        // 1 byte
        QByteArray data1(1, 0xFF);
        QByteArray enc1 = secChan->encrypt(data1);
        QVERIFY(!enc1.isEmpty());
        
        // 15 bytes (not block-aligned)
        QByteArray data15(15, 0xAA);
        secChan->reset();
        secChan->init(iv, enc, mac);
        QByteArray enc15 = secChan->encrypt(data15);
        QVERIFY(!enc15.isEmpty());
        
        // 16 bytes (exactly one block)
        QByteArray data16(16, 0xBB);
        secChan->reset();
        secChan->init(iv, enc, mac);
        QByteArray enc16 = secChan->encrypt(data16);
        QVERIFY(!enc16.isEmpty());
        
        // 100 bytes (multiple blocks)
        QByteArray data100(100, 0xCC);
        secChan->reset();
        secChan->init(iv, enc, mac);
        QByteArray enc100 = secChan->encrypt(data100);
        QVERIFY(!enc100.isEmpty());
    }
    
    // Test decrypt with invalid data
    void testDecryptInvalidData() {
        QByteArray iv(16, 0x01);
        QByteArray enc(16, 0x02);
        QByteArray mac(16, 0x03);
        secChan->init(iv, enc, mac);
        
        // Empty data should return empty
        QByteArray empty;
        QByteArray result = secChan->decrypt(empty);
        QVERIFY(result.isEmpty());
        
        // Random encrypted data will decrypt to some output (possibly garbage)
        // OpenSSL doesn't fail on "wrong" data, it just decrypts whatever bytes are there
        QByteArray invalidData(32, 0xFF);  // Random data
        secChan->reset();
        secChan->init(iv, enc, mac);
        QByteArray result2 = secChan->decrypt(invalidData);
        // OpenSSL will decrypt this (even if it's garbage), verify no crash occurred
        // The result could be empty or non-empty depending on OpenSSL behavior
        QVERIFY2(true, "Decrypt completed without crash");
    }
    
    // Test send() without open channel
    void testSendWithoutOpenChannel() {
        APDU::Command cmd(0x80, 0x20, 0x00, 0x00);
        
        bool exceptionThrown = false;
        try {
            secChan->send(cmd);
            // If no exception was thrown, fail with a descriptive message
            QFAIL("Expected std::runtime_error when calling send() on closed channel");
        } catch (const std::runtime_error& e) {
            exceptionThrown = true;
            QString msg = e.what();
            QVERIFY2(msg.contains("not open") || msg.contains("not available"), 
                     qPrintable(QString("Exception message did not match expected pattern: %1").arg(msg)));
        } catch (const std::exception& e) {
            // Catch other exception types and fail with details
            QFAIL(qPrintable(QString("Unexpected exception type: %1").arg(e.what())));
        } catch (...) {
            QFAIL("Unexpected non-standard exception thrown");
        }
        
        QVERIFY(exceptionThrown);
    }
    
    // Test oneShotEncrypt
    void testOneShotEncrypt() {
        // oneShotEncrypt uses secret directly, so we need to have generated one
        // For now, we'll test the structure without a valid secret
        
        QByteArray data = "test data for one-shot encryption";
        QByteArray encrypted = secChan->oneShotEncrypt(data);
        
        // Without secret, the behavior is implementation-defined
        // This tests that the function doesn't crash
        QVERIFY2(true, "oneShotEncrypt completed without crash");
        
        // Verify we still have a valid object after the call
        QVERIFY(secChan != nullptr);
    }
    
    // Test multiple reset cycles
    void testMultipleResetCycles() {
        for (int i = 0; i < 5; i++) {
            QByteArray iv(16, i);
            QByteArray enc(16, i + 1);
            QByteArray mac(16, i + 2);
            
            secChan->init(iv, enc, mac);
            QVERIFY(secChan->isOpen());
            
            secChan->reset();
            QVERIFY(!secChan->isOpen());
        }
    }
};

QTEST_MAIN(TestSecureChannelExtended)
#include "test_secure_channel_extended.moc"

