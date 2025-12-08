/**
 * Unit tests for APDU::Response
 */

#include <QTest>
#include <QDebug>
#include "keycard-qt/apdu/response.h"

class TestAPDUResponse : public QObject {
    Q_OBJECT
    
private slots:
    void initTestCase() {
    }
    
    void testSuccessResponse() {
        // Test: Response with SW=0x9000 (success)
        QByteArray raw = QByteArray::fromHex("9000");
        Keycard::APDU::Response resp(raw);
        
        QCOMPARE(resp.sw(), (uint16_t)0x9000);
        QVERIFY(resp.isOK());
        QVERIFY(resp.data().isEmpty());
    }
    
    void testResponseWithData() {
        // Test: Response with data + SW
        QByteArray raw = QByteArray::fromHex("AABBCCDD9000");
        Keycard::APDU::Response resp(raw);
        
        QCOMPARE(resp.sw(), (uint16_t)0x9000);
        QVERIFY(resp.isOK());
        QCOMPARE(resp.data(), QByteArray::fromHex("AABBCCDD"));
        QCOMPARE(resp.data().size(), 4);
    }
    
    void testErrorResponse() {
        // Test: Error response (SW != 9000)
        QByteArray raw = QByteArray::fromHex("6985"); // Conditions not satisfied
        Keycard::APDU::Response resp(raw);
        
        QCOMPARE(resp.sw(), (uint16_t)0x6985);
        QVERIFY(!resp.isOK());
        QVERIFY(resp.data().isEmpty());
    }
    
    void testWrongPINResponse() {
        // Test: Wrong PIN (SW=63CX where X = remaining attempts)
        QByteArray raw = QByteArray::fromHex("63C3"); // 3 attempts remaining
        Keycard::APDU::Response resp(raw);
        
        QCOMPARE(resp.sw(), (uint16_t)0x63C3);
        QVERIFY(!resp.isOK());
        
        // Check remaining attempts
        QVERIFY((resp.sw() & 0x63C0) == 0x63C0); // Wrong PIN pattern
        int remaining = resp.sw() & 0x000F;
        QCOMPARE(remaining, 3);
    }
    
    void testLongDataResponse() {
        // Test: Response with long data
        QByteArray data(100, 0x55);
        QByteArray raw = data + QByteArray::fromHex("9000");
        Keycard::APDU::Response resp(raw);
        
        QVERIFY(resp.isOK());
        QCOMPARE(resp.data().size(), 100);
        QCOMPARE(resp.data(), data);
    }
    
    void testMinimalResponse() {
        // Test: Minimal 2-byte response (just SW)
        QByteArray raw = QByteArray::fromHex("9000");
        Keycard::APDU::Response resp(raw);
        
        QCOMPARE(raw.size(), 2);
        QVERIFY(resp.isOK());
        QVERIFY(resp.data().isEmpty());
    }
    
    void testInvalidResponse() {
        // Test: Invalid response (less than 2 bytes)
        QByteArray raw = QByteArray::fromHex("90"); // Only 1 byte
        Keycard::APDU::Response resp(raw);
        
        // Should handle gracefully (SW=0 or similar)
        QVERIFY(!resp.isOK());
    }
    
    void testVariousStatusWords() {
        // Test various common status words
        struct TestCase {
            const char* hex;
            uint16_t expectedSW;
            bool shouldBeOK;
        };
        
        TestCase cases[] = {
            {"9000", 0x9000, true},  // Success
            {"6985", 0x6985, false}, // Conditions not satisfied
            {"6982", 0x6982, false}, // Security not satisfied
            {"6A82", 0x6A82, false}, // File not found
            {"6A80", 0x6A80, false}, // Incorrect data
            {"6D00", 0x6D00, false}, // INS not supported
            {"6E00", 0x6E00, false}, // CLA not supported
        };
        
        for (const auto& testCase : cases) {
            QByteArray raw = QByteArray::fromHex(testCase.hex);
            Keycard::APDU::Response resp(raw);
            
            QCOMPARE(resp.sw(), testCase.expectedSW);
            QCOMPARE(resp.isOK(), testCase.shouldBeOK);
        }
    }
    
    void cleanupTestCase() {
    }
};

QTEST_MAIN(TestAPDUResponse)
#include "test_apdu_response.moc"

