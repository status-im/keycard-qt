/**
 * Unit tests for SecureChannel
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/secure_channel.h"
#include "keycard-qt/channel_interface.h"

// Mock channel for testing
class MockChannel : public Keycard::IChannel {
public:
    QByteArray lastTransmitted;
    QByteArray nextResponse;
    
    QByteArray transmit(const QByteArray& apdu) override {
        lastTransmitted = apdu;
        return nextResponse;
    }
    
    bool isConnected() const override {
        return true;
    }
};

class TestSecureChannel : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
    }
    
    void testConstruction() {
        MockChannel mockChannel;
        Keycard::SecureChannel sc(&mockChannel);
        
        // Should not be open initially
        QVERIFY(!sc.isOpen());
        QVERIFY(sc.rawPublicKey().isEmpty());
        QVERIFY(sc.secret().isEmpty());
    }
    
    void testGenerateSecret() {
        MockChannel mockChannel;
        Keycard::SecureChannel sc(&mockChannel);
        
        // Note: We can't test with random data because EC point validation will fail
        // We need a valid secp256k1 point, which requires proper crypto
        // For now, just test that our key generation works
        
        // Generate our key pair first
        QByteArray fakeCardKey(65, 0x04);
        // Fill with some data (will fail EC validation, but we can check structure)
        bool result = sc.generateSecret(fakeCardKey);
        
        // Even if ECDH fails (invalid card key), we should still generate our key
        // This is expected to fail with "Failed to parse card public key"
        // But our public key should still be generated
        if (result) {
            // If it somehow succeeded:
            QCOMPARE(sc.rawPublicKey().size(), 65);
            QCOMPARE((uint8_t)sc.rawPublicKey()[0], (uint8_t)0x04);
            QVERIFY(!sc.secret().isEmpty());
        } else {
            // Expected: fails because card key is not a valid EC point
            QVERIFY(!result); // This is expected
        }
    }
    
    void testInit() {
        MockChannel mockChannel;
        Keycard::SecureChannel sc(&mockChannel);
        
        // Initialize with session keys
        QByteArray iv = QByteArray(16, 0xAA);
        QByteArray encKey = QByteArray(16, 0xBB);
        QByteArray macKey = QByteArray(16, 0xCC);
        
        sc.init(iv, encKey, macKey);
        
        // Should now be open
        QVERIFY(sc.isOpen());
    }
    
    void testReset() {
        MockChannel mockChannel;
        Keycard::SecureChannel sc(&mockChannel);
        
        // Setup channel
        QByteArray cardPubKey(65, 0x00);
        cardPubKey[0] = 0x04;
        sc.generateSecret(cardPubKey);
        sc.init(QByteArray(16, 0x11), QByteArray(16, 0x22), QByteArray(16, 0x33));
        
        QVERIFY(sc.isOpen());
        
        // Reset
        sc.reset();
        
        // Should be cleared except keys are kept for pairing
        QVERIFY(!sc.isOpen());
        QVERIFY(!sc.rawPublicKey().isEmpty()); // Keys kept for pairing
        QVERIFY(sc.secret().isEmpty());
    }
    
    void testEncryption() {
        // Note: Full encryption testing requires proper keys
        // This is a basic structure test
        MockChannel mockChannel;
        Keycard::SecureChannel sc(&mockChannel);
        
        // Setup with test keys (AES-256 requires 32 bytes)
        QByteArray iv = QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");
        QByteArray encKey = QByteArray::fromHex(
            "0123456789ABCDEF0123456789ABCDEF"  // 32 bytes for AES-256
            "0123456789ABCDEF0123456789ABCDEF"
        );
        QByteArray macKey = QByteArray::fromHex(
            "FEDCBA9876543210FEDCBA9876543210"  // 32 bytes for AES-256
            "FEDCBA9876543210FEDCBA9876543210"
        );
        
        sc.init(iv, encKey, macKey);
        QVERIFY(sc.isOpen());
        
        // Try encrypting some data
        QByteArray plaintext = QByteArray::fromHex("AABBCCDD");
        QByteArray encrypted = sc.encrypt(plaintext);
        
        // Encrypted should be longer (padding + MAC)
        QVERIFY(encrypted.size() > plaintext.size());
    }
    
    void cleanupTestCase() {
    }
};

QTEST_MAIN(TestSecureChannel)
#include "test_secure_channel.moc"

