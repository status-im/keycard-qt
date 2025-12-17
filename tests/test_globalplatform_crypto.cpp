/**
 * Unit tests for GlobalPlatform SCP02 cryptographic functions
 * 
 * Tests 3DES key derivation, MAC calculation, and padding
 * based on the reference implementation in keycard-go
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/globalplatform/gp_crypto.h"

using namespace Keycard::GlobalPlatform;

class TestGlobalPlatformCrypto : public QObject
{
    Q_OBJECT

private slots:
    /**
     * Test DES padding (ISO 9797-1 Method 2)
     */
    void testDESPadding()
    {
        // Test 1: Data that needs padding
        QByteArray data1 = QByteArray::fromHex("0102030405");
        QByteArray padded1 = Crypto::appendDESPadding(data1);
        
        // Should pad to 8 bytes: 01 02 03 04 05 80 00 00
        QCOMPARE(padded1.size(), 8);
        QCOMPARE(padded1.toHex(), QByteArray("0102030405800000"));
        
        // Test 2: Data that exactly fills a block (still needs padding)
        QByteArray data2 = QByteArray::fromHex("0102030405060708");
        QByteArray padded2 = Crypto::appendDESPadding(data2);
        
        // Should add full block: 01...08 80 00 00 00 00 00 00 00
        QCOMPARE(padded2.size(), 16);
        QCOMPARE(padded2.toHex(), QByteArray("01020304050607088000000000000000"));
        
        // Test 3: Empty data
        QByteArray data3;
        QByteArray padded3 = Crypto::appendDESPadding(data3);
        
        // Should pad to 8 bytes: 80 00 00 00 00 00 00 00
        QCOMPARE(padded3.size(), 8);
        QCOMPARE(padded3.toHex(), QByteArray("8000000000000000"));
    }
    
    /**
     * Test key derivation
     * 
     * SCP02 derives session keys from base keys using:
     * sessionKey = 3DES-CBC(baseKey, sequence || purpose)
     */
    void testKeyDerivation()
    {
        // Test with known values
        QByteArray baseKey = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
        QByteArray sequence = QByteArray::fromHex("0001");
        
        // Test ENC key derivation
        QByteArray encKey = Crypto::deriveKey(baseKey, sequence, Crypto::DERIVATION_PURPOSE_ENC());
        QVERIFY(!encKey.isEmpty());
        QCOMPARE(encKey.size(), 16);
        qDebug() << "Derived ENC key:" << encKey.toHex();
        
        // Test MAC key derivation
        QByteArray macKey = Crypto::deriveKey(baseKey, sequence, Crypto::DERIVATION_PURPOSE_MAC());
        QVERIFY(!macKey.isEmpty());
        QCOMPARE(macKey.size(), 16);
        qDebug() << "Derived MAC key:" << macKey.toHex();
        
        // ENC and MAC keys should be different
        QVERIFY(encKey != macKey);
    }
    
    /**
     * Test 3DES MAC calculation
     * 
     * Used for verifying card cryptogram and generating host cryptogram
     */
    void testMac3DES()
    {
        // Test with known values
        QByteArray key = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
        QByteArray data = QByteArray::fromHex("0102030405060708");
        QByteArray iv = Crypto::NULL_BYTES_8();
        
        // Calculate MAC
        QByteArray mac = Crypto::mac3DES(key, data, iv);
        
        // MAC should be 8 bytes
        QCOMPARE(mac.size(), 8);
        qDebug() << "MAC:" << mac.toHex();
        
        // MAC should be deterministic
        QByteArray mac2 = Crypto::mac3DES(key, data, iv);
        QCOMPARE(mac, mac2);
        
        // Different data should produce different MAC
        QByteArray differentData = QByteArray::fromHex("0807060504030201");
        QByteArray differentMac = Crypto::mac3DES(key, differentData, iv);
        QVERIFY(mac != differentMac);
    }
    
    /**
     * Test full 3DES MAC (retail MAC)
     * 
     * This is the MAC used for SCP02 command authentication
     */
    void testMacFull3DES()
    {
        // Test with known values
        QByteArray key = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
        QByteArray data = QByteArray::fromHex("8050000008");  // CLA INS P1 P2 Lc (INITIALIZE UPDATE)
        QByteArray iv = Crypto::NULL_BYTES_8();
        
        // Calculate full MAC
        QByteArray mac = Crypto::macFull3DES(key, data, iv);
        
        // MAC should be 8 bytes
        QCOMPARE(mac.size(), 8);
        qDebug() << "Full MAC:" << mac.toHex();
        
        // Test with longer data
        QByteArray longData = QByteArray::fromHex("8050000008" "0102030405060708");
        QByteArray longMac = Crypto::macFull3DES(key, longData, iv);
        QCOMPARE(longMac.size(), 8);
        qDebug() << "Long MAC:" << longMac.toHex();
    }
    
    /**
     * Test cryptogram verification
     * 
     * Used to verify the card's authenticity during secure channel setup
     */
    void testCryptogramVerification()
    {
        // For a real test, we'd need actual card responses
        // Here we test that the function runs without crashing
        
        QByteArray encKey = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
        QByteArray hostChallenge = QByteArray::fromHex("0102030405060708");
        QByteArray cardChallenge = QByteArray::fromHex("0807060504030201");
        
        // Calculate what the card cryptogram should be
        QByteArray data = hostChallenge + cardChallenge;
        QByteArray paddedData = Crypto::appendDESPadding(data);
        QByteArray expectedCryptogram = Crypto::mac3DES(encKey, paddedData, Crypto::NULL_BYTES_8());
        
        // Verify it
        bool verified = Crypto::verifyCryptogram(encKey, hostChallenge, cardChallenge, expectedCryptogram);
        QVERIFY(verified);
        
        // Wrong cryptogram should fail
        QByteArray wrongCryptogram = QByteArray(8, 0xFF);
        bool wrongVerified = Crypto::verifyCryptogram(encKey, hostChallenge, cardChallenge, wrongCryptogram);
        QVERIFY(!wrongVerified);
    }
    
    /**
     * Test ICV encryption
     * 
     * Used for MAC chaining in SCP02 wrapper
     */
    void testEncryptICV()
    {
        QByteArray macKey = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
        QByteArray icv = QByteArray::fromHex("0102030405060708");
        
        // Encrypt ICV
        QByteArray encrypted = Crypto::encryptICV(macKey, icv);
        
        // Should be 8 bytes
        QCOMPARE(encrypted.size(), 8);
        qDebug() << "Encrypted ICV:" << encrypted.toHex();
        
        // Should be deterministic
        QByteArray encrypted2 = Crypto::encryptICV(macKey, icv);
        QCOMPARE(encrypted, encrypted2);
        
        // Different ICV should produce different result
        QByteArray differentIcv = Crypto::NULL_BYTES_8();
        QByteArray differentEncrypted = Crypto::encryptICV(macKey, differentIcv);
        QVERIFY(encrypted != differentEncrypted);
    }
};

QTEST_MAIN(TestGlobalPlatformCrypto)
#include "test_globalplatform_crypto.moc"



