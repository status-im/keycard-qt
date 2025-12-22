/**
 * Unit tests for APDU::Command
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/apdu/command.h"

class TestAPDUCommand : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
    }
    
    void testBasicCommand() {
        // Test: CLA=0x80, INS=0xA4, P1=0x04, P2=0x00, no data, no Le
        Keycard::APDU::Command cmd(0x80, 0xA4, 0x04, 0x00);
        
        QCOMPARE(cmd.cla(), (uint8_t)0x80);
        QCOMPARE(cmd.ins(), (uint8_t)0xA4);
        QCOMPARE(cmd.p1(), (uint8_t)0x04);
        QCOMPARE(cmd.p2(), (uint8_t)0x00);
        QVERIFY(cmd.data().isEmpty());
        QVERIFY(!cmd.hasLe());
        
        // Serialize: should be just 4 bytes (CLA INS P1 P2)
        QByteArray serialized = cmd.serialize();
        QCOMPARE(serialized.size(), 4);
        QCOMPARE((uint8_t)serialized[0], (uint8_t)0x80);
        QCOMPARE((uint8_t)serialized[1], (uint8_t)0xA4);
        QCOMPARE((uint8_t)serialized[2], (uint8_t)0x04);
        QCOMPARE((uint8_t)serialized[3], (uint8_t)0x00);
    }
    
    void testCommandWithData() {
        // Test: Command with data (Lc + data)
        Keycard::APDU::Command cmd(0x80, 0xFE, 0x00, 0x00);
        QByteArray data = QByteArray::fromHex("AABBCCDD");
        cmd.setData(data);
        
        QCOMPARE(cmd.data(), data);
        QCOMPARE(cmd.data().size(), 4);
        
        // Serialize: CLA INS P1 P2 Lc Data
        QByteArray serialized = cmd.serialize();
        QCOMPARE(serialized.size(), 4 + 1 + 4); // Header + Lc + Data
        QCOMPARE((uint8_t)serialized[4], (uint8_t)4); // Lc
        QCOMPARE(serialized.mid(5, 4), data);
    }
    
    void testCommandWithLe() {
        // Test: Command with Le (expected response length)
        Keycard::APDU::Command cmd(0x00, 0xA4, 0x04, 0x00);
        cmd.setLe(0); // Le=0 means 256 bytes in ISO 7816-4
        
        QVERIFY(cmd.hasLe());
        // Note: Le stores the actual value, not the encoded value
        QCOMPARE(cmd.le(), 0); // Stored as 0, encoded as 0 (means 256)
        
        // Serialize: CLA INS P1 P2 Lc Le (Case 4 short with Lc=0)
        // When only Le is set (no data), the implementation outputs Lc=0 for compatibility
        QByteArray serialized = cmd.serialize();
        QCOMPARE(serialized.size(), 6); // CLA INS P1 P2 Lc=0 Le
        QCOMPARE((uint8_t)serialized[4], (uint8_t)0x00); // Lc=0
        QCOMPARE((uint8_t)serialized[5], (uint8_t)0x00); // Le=0 means 256
    }
    
    void testCommandWithDataAndLe() {
        // Test: Command with both data and Le
        Keycard::APDU::Command cmd(0x80, 0x12, 0x00, 0x00);
        QByteArray data = QByteArray::fromHex("112233");
        cmd.setData(data);
        cmd.setLe(64);
        
        // Serialize: CLA INS P1 P2 Lc Data Le
        QByteArray serialized = cmd.serialize();
        QCOMPARE(serialized.size(), 4 + 1 + 3 + 1); // Header + Lc + Data + Le
        QCOMPARE((uint8_t)serialized[4], (uint8_t)3); // Lc
        QCOMPARE(serialized.mid(5, 3), data);
        QCOMPARE((uint8_t)serialized[8], (uint8_t)64); // Le
    }
    
    void testLongData() {
        // Test: Command with data > 255 bytes (extended length)
        Keycard::APDU::Command cmd(0x80, 0xD0, 0x00, 0x00);
        QByteArray longData(300, 0xAA);
        cmd.setData(longData);
        
        QCOMPARE(cmd.data().size(), 300);
        
        // Extended length encoding: CLA INS P1 P2 0x00 LcHi LcLo Data
        QByteArray serialized = cmd.serialize();
        QCOMPARE(serialized.size(), 4 + 3 + 300); // Header + extended Lc + Data
        QCOMPARE((uint8_t)serialized[4], (uint8_t)0x00); // Extended length marker
        QCOMPARE((uint8_t)serialized[5], (uint8_t)0x01); // Lc high byte
        QCOMPARE((uint8_t)serialized[6], (uint8_t)0x2C); // Lc low byte (300 = 0x012C)
    }
    
    void testSelectCommand() {
        // Test: Real SELECT command example
        QByteArray aid = QByteArray::fromHex("A000000804000100000000000001");
        Keycard::APDU::Command cmd(0x00, 0xA4, 0x04, 0x00);
        cmd.setData(aid);
        cmd.setLe(0); // Expect full response
        
        QByteArray serialized = cmd.serialize();
        QVERIFY(serialized.size() > 0);
        QCOMPARE((uint8_t)serialized[0], (uint8_t)0x00); // CLA
        QCOMPARE((uint8_t)serialized[1], (uint8_t)0xA4); // INS SELECT
        QCOMPARE((uint8_t)serialized[4], (uint8_t)aid.size()); // Lc
    }
    
    void cleanupTestCase() {
    }
};

QTEST_MAIN(TestAPDUCommand)
#include "test_apdu_command.moc"

