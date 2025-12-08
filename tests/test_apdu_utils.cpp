/**
 * Unit tests for APDU::Utils (padding, TLV, etc.)
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/apdu/utils.h"

class TestAPDUUtils : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
    }
    
    void testPadding() {
        // Test: Pad to 16-byte boundary (ISO 7816-4 padding)
        QByteArray data = QByteArray::fromHex("AABBCC");
        QByteArray padded = Keycard::APDU::Utils::pad(data, 16);
        
        // Should be padded to 16 bytes: 3 data + 1 (0x80) + 12 (0x00)
        QCOMPARE(padded.size(), 16);
        QCOMPARE(padded.left(3), data);
        QCOMPARE((uint8_t)padded[3], (uint8_t)0x80); // Padding start marker
        
        // Rest should be zeros
        for (int i = 4; i < 16; i++) {
            QCOMPARE((uint8_t)padded[i], (uint8_t)0x00);
        }
    }
    
    void testUnpadding() {
        // Test: Remove ISO 7816-4 padding
        QByteArray padded = QByteArray::fromHex("AABBCC800000000000000000000000");
        QByteArray unpadded = Keycard::APDU::Utils::unpad(padded);
        
        QCOMPARE(unpadded, QByteArray::fromHex("AABBCC"));
    }
    
    void testPaddingAlreadyAligned() {
        // Test: Data already aligned to block size
        QByteArray data(16, 0xAA);
        QByteArray padded = Keycard::APDU::Utils::pad(data, 16);
        
        // Should add full padding block
        QCOMPARE(padded.size(), 32);
        QCOMPARE(padded.left(16), data);
        QCOMPARE((uint8_t)padded[16], (uint8_t)0x80);
    }
    
    void testPaddingDifferentBlockSizes() {
        // Test: Padding with different block sizes
        QByteArray data = QByteArray::fromHex("AABBCCDD");
        
        // 8-byte blocks
        QByteArray padded8 = Keycard::APDU::Utils::pad(data, 8);
        QCOMPARE(padded8.size(), 8);
        
        // 32-byte blocks
        QByteArray padded32 = Keycard::APDU::Utils::pad(data, 32);
        QCOMPARE(padded32.size(), 32);
    }
    
    void testEmptyPadding() {
        // Test: Padding empty data
        QByteArray empty;
        QByteArray padded = Keycard::APDU::Utils::pad(empty, 16);
        
        QCOMPARE(padded.size(), 16);
        QCOMPARE((uint8_t)padded[0], (uint8_t)0x80);
        for (int i = 1; i < 16; i++) {
            QCOMPARE((uint8_t)padded[i], (uint8_t)0x00);
        }
    }
    
    void testRoundTripPadding() {
        // Test: Pad then unpad should give original data
        QByteArray original = QByteArray::fromHex("0102030405060708090A");
        QByteArray padded = Keycard::APDU::Utils::pad(original, 16);
        QByteArray unpadded = Keycard::APDU::Utils::unpad(padded);
        
        QCOMPARE(unpadded, original);
    }
    
    void testHexConversion() {
        // Test: Hex string conversion
        QByteArray data = QByteArray::fromHex("DEADBEEF");
        QString hex = data.toHex().toUpper();
        
        QCOMPARE(hex, QString("DEADBEEF"));
        QCOMPARE(data.size(), 4);
    }
    
    void testByteManipulation() {
        // Test: Individual byte access and manipulation
        QByteArray data = QByteArray::fromHex("AABBCCDD");
        
        QCOMPARE((uint8_t)data[0], (uint8_t)0xAA);
        QCOMPARE((uint8_t)data[1], (uint8_t)0xBB);
        QCOMPARE((uint8_t)data[2], (uint8_t)0xCC);
        QCOMPARE((uint8_t)data[3], (uint8_t)0xDD);
        
        // Modify
        data[0] = 0x11;
        QCOMPARE((uint8_t)data[0], (uint8_t)0x11);
    }
    
    void cleanupTestCase() {
    }
};

QTEST_MAIN(TestAPDUUtils)
#include "test_apdu_utils.moc"

