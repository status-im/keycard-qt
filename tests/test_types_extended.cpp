#include <QTest>
#include "keycard-qt/types.h"

using namespace Keycard;

class TestTypesExtended : public QObject {
    Q_OBJECT
    
private slots:
    // Test ExportedKey
    void testExportedKeyDefault() {
        ExportedKey key;
        
        QVERIFY(key.publicKey.isEmpty());
        QVERIFY(key.privateKey.isEmpty());
        QVERIFY(key.chainCode.isEmpty());
    }
    
    void testExportedKeyWithData() {
        ExportedKey key;
        key.publicKey = QByteArray(65, 0x04);  // Uncompressed public key
        key.privateKey = QByteArray(32, 0xFF);
        key.chainCode = QByteArray(32, 0xAA);
        
        QCOMPARE(key.publicKey.size(), 65);
        QCOMPARE(key.privateKey.size(), 32);
        QCOMPARE(key.chainCode.size(), 32);
        QVERIFY(!key.publicKey.isEmpty());
    }
    
    // Test Signature
    void testSignatureDefault() {
        Signature sig;
        
        QVERIFY(sig.r.isEmpty());
        QVERIFY(sig.s.isEmpty());
        QCOMPARE(sig.v, static_cast<uint8_t>(0));
        QVERIFY(sig.publicKey.isEmpty());
    }
    
    void testSignatureWithECDSAData() {
        Signature sig;
        sig.r = QByteArray(32, 0xAA);  // R component
        sig.s = QByteArray(32, 0xBB);  // S component
        sig.v = 27;                     // Recovery ID (27 or 28 for Ethereum)
        sig.publicKey = QByteArray(65, 0x04);
        
        QCOMPARE(sig.r.size(), 32);
        QCOMPARE(sig.s.size(), 32);
        QCOMPARE(sig.v, static_cast<uint8_t>(27));
        QCOMPARE(sig.publicKey.size(), 65);
    }
    
    void testSignatureRecoveryIDs() {
        // Test various recovery IDs
        Signature sig1;
        sig1.v = 0;
        QCOMPARE(sig1.v, static_cast<uint8_t>(0));
        
        Signature sig2;
        sig2.v = 1;
        QCOMPARE(sig2.v, static_cast<uint8_t>(1));
        
        Signature sig3;
        sig3.v = 27;  // Ethereum-style
        QCOMPARE(sig3.v, static_cast<uint8_t>(27));
        
        Signature sig4;
        sig4.v = 28;  // Ethereum-style
        QCOMPARE(sig4.v, static_cast<uint8_t>(28));
    }
    
    // Test Metadata
    void testMetadataDefault() {
        Metadata meta;
        
        QVERIFY(meta.name.isEmpty());
        QVERIFY(meta.paths.isEmpty());
    }
    
    void testMetadataWithWalletInfo() {
        Metadata meta;
        meta.name = "My Wallet";
        meta.paths = {0x8000002C, 0x8000003C, 0x80000000};  // BIP44 paths
        
        QCOMPARE(meta.name, QString("My Wallet"));
        QCOMPARE(meta.paths.size(), 3);
        QCOMPARE(meta.paths[0], static_cast<uint32_t>(0x8000002C));
        QCOMPARE(meta.paths[1], static_cast<uint32_t>(0x8000003C));
        QCOMPARE(meta.paths[2], static_cast<uint32_t>(0x80000000));
    }
    
    void testMetadataEmptyPaths() {
        Metadata meta;
        meta.name = "Wallet Without Paths";
        
        QVERIFY(!meta.name.isEmpty());
        QVERIFY(meta.paths.isEmpty());
        QCOMPARE(meta.paths.size(), 0);
    }
    
    // Test Secrets validation
    void testSecretsValidFormats() {
        Secrets s1("123456", "123456789012", "password");
        QCOMPARE(s1.pin.length(), 6);
        QCOMPARE(s1.puk.length(), 12);
        QVERIFY(!s1.pairingPassword.isEmpty());
        
        Secrets s2("000000", "000000000000", "a");
        QCOMPARE(s2.pin, QString("000000"));
        QCOMPARE(s2.puk, QString("000000000000"));
        QCOMPARE(s2.pairingPassword, QString("a"));
    }
    
    void testSecretsDefault() {
        Secrets s;
        QVERIFY(s.pin.isEmpty());
        QVERIFY(s.puk.isEmpty());
        QVERIFY(s.pairingPassword.isEmpty());
    }
    
    // Test ApplicationInfo edge cases
    void testApplicationInfoAllFalse() {
        ApplicationInfo info;
        info.installed = false;
        info.initialized = false;
        
        QVERIFY(!info.installed);
        QVERIFY(!info.initialized);
        QCOMPARE(info.appVersion, static_cast<uint8_t>(0));
        QCOMPARE(info.availableSlots, static_cast<uint8_t>(0));
    }
    
    void testApplicationInfoMaxSlots() {
        ApplicationInfo info;
        info.availableSlots = 255;  // Max uint8_t
        
        QCOMPARE(info.availableSlots, static_cast<uint8_t>(255));
    }
    
    // Test ApplicationStatus edge cases
    void testApplicationStatusBlocked() {
        ApplicationStatus status;
        status.pinRetryCount = 0;
        status.pukRetryCount = 0;
        
        QCOMPARE(status.pinRetryCount, static_cast<uint8_t>(0));
        QCOMPARE(status.pukRetryCount, static_cast<uint8_t>(0));
    }
    
    void testApplicationStatusMaxRetries() {
        ApplicationStatus status;
        status.pinRetryCount = 3;
        status.pukRetryCount = 5;
        
        QCOMPARE(status.pinRetryCount, static_cast<uint8_t>(3));
        QCOMPARE(status.pukRetryCount, static_cast<uint8_t>(5));
    }
    
    // Test PairingInfo edge cases
    void testPairingInfoNegativeIndex() {
        PairingInfo p1;
        p1.index = -1;
        QVERIFY(!p1.isValid());
        
        PairingInfo p2;
        p2.key = QByteArray(32, 0xAA);
        p2.index = -1;
        QVERIFY(!p2.isValid());  // Invalid due to negative index
    }
    
    void testPairingInfoEmptyKey() {
        PairingInfo p;
        p.key = QByteArray();
        p.index = 0;
        QVERIFY(!p.isValid());  // Invalid due to empty key
    }
    
    void testPairingInfoMaxIndex() {
        PairingInfo p;
        p.key = QByteArray(32, 0xAA);
        p.index = 255;
        QVERIFY(p.isValid());
        QCOMPARE(p.index, 255);
    }
    
    // Test APDU constants
    void testAPDUConstants() {
        // Verify key constants are defined correctly
        QCOMPARE(APDU::CLA, static_cast<uint8_t>(0x80));
        QCOMPARE(APDU::CLA_ISO7816, static_cast<uint8_t>(0x00));
        
        QCOMPARE(APDU::INS_SELECT, static_cast<uint8_t>(0xA4));
        QCOMPARE(APDU::INS_INIT, static_cast<uint8_t>(0xFE));
        QCOMPARE(APDU::INS_PAIR, static_cast<uint8_t>(0x12));
        
        QCOMPARE(APDU::SW_OK, static_cast<uint16_t>(0x9000));
        QCOMPARE(APDU::SW_SECURITY_CONDITION_NOT_SATISFIED, static_cast<uint16_t>(0x6982));
        QCOMPARE(APDU::SW_AUTHENTICATION_METHOD_BLOCKED, static_cast<uint16_t>(0x6983));
    }
    
    void testAPDUP1Parameters() {
        QCOMPARE(APDU::P1GetStatusApplication, static_cast<uint8_t>(0x00));
        QCOMPARE(APDU::P1GetStatusKeyPath, static_cast<uint8_t>(0x01));
        
        QCOMPARE(APDU::P1PairFirstStep, static_cast<uint8_t>(0x00));
        QCOMPARE(APDU::P1PairFinalStep, static_cast<uint8_t>(0x01));
    }
    
    void testAPDUP2Parameters() {
        // Test actual APDU P2 parameter values from types.h
        QCOMPARE(APDU::P2ExportKeyPrivateAndPublic, static_cast<uint8_t>(0x00));  // Export both keys
        QCOMPARE(APDU::P2ExportKeyPublicOnly, static_cast<uint8_t>(0x01));        // Export public only
        QCOMPARE(APDU::P2ExportKeyExtendedPublic, static_cast<uint8_t>(0x02));    // Export extended public
    }
};

QTEST_MAIN(TestTypesExtended)
#include "test_types_extended.moc"

