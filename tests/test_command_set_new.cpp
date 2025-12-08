#include <QTest>
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "mocks/mock_backend.h"
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

class TestCommandSetNew : public QObject {
    Q_OBJECT
    
private:
    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(true);
        auto channel = std::make_shared<KeycardChannel>(mock);
        mock->simulateCardInserted();
        return channel;
    }
    
    
private slots:
    void initTestCase() {
    }
    
    void testConstruction() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        QVERIFY(cmd.lastError().isEmpty());
    }
    
    void testSelectCommand() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        QByteArray mockResponse = QByteArray::fromHex("8041");
        mockResponse.append(QByteArray(65, 0x04));
        mockResponse.append(QByteArray::fromHex("9000"));
        mock->queueResponse(mockResponse);
        
        CommandSet cmd(channel, nullptr, nullptr);
        ApplicationInfo info = cmd.select();
        
        QVERIFY(info.installed);
        QVERIFY(mock->getTransmitCount() > 0);
    }
    
    void testSelectError() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        mock->queueResponse(QByteArray::fromHex("6A82"));
        
        CommandSet cmd(channel, nullptr, nullptr);
        ApplicationInfo info = cmd.select();
        
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testVerifyPINWithoutSecureChannel() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.verifyPIN("000000");
        
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testChangePIN() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray pairingKey(32, 0xAB);
        PairingInfo pairingInfo(pairingKey, 1);
        QByteArray mockIV(16, 0x00);
        QByteArray mockEncKey(16, 0xEE);
        QByteArray mockMacKey(16, 0xDD);
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mock->simulateCardInserted();
        QTRY_VERIFY(channel->isConnected());
        
        mock->queueResponse(QByteArray::fromHex("9000"));
        
        QVERIFY(channel->isConnected());
        
        bool result = cmd.changePIN("123456");
        
        QVERIFY(result);
        QVERIFY(mock->getTransmitCount() > 0);
        QVERIFY(cmd.lastError().isEmpty());
    }
    
    void testChangePINWithoutSecureChannel() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.changePIN("123456");
        
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
        QVERIFY(cmd.lastError().contains("APDU error") || cmd.lastError().contains("Secure channel"));
    }
    
    void testChangePUK() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray pairingKey(32, 0xAB);
        PairingInfo pairingInfo(pairingKey, 1);
        QByteArray mockIV(16, 0x00);
        QByteArray mockEncKey(16, 0xEE);
        QByteArray mockMacKey(16, 0xDD);
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mock->simulateCardInserted();
        QTRY_VERIFY(channel->isConnected());
        
        mock->queueResponse(QByteArray::fromHex("9000"));
        
        bool result = cmd.changePUK("123456789012");
        
        QVERIFY(result);
        QVERIFY(mock->getTransmitCount() > 0);
    }
    
    void testChangePUKWithoutSecureChannel() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.changePUK("123456789012");
        
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testUnblockPIN() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray pairingKey(32, 0xAB);
        PairingInfo pairingInfo(pairingKey, 1);
        QByteArray mockIV(16, 0x00);
        QByteArray mockEncKey(16, 0xEE);
        QByteArray mockMacKey(16, 0xDD);
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mock->simulateCardInserted();
        QTRY_VERIFY(channel->isConnected());
        
        mock->queueResponse(QByteArray::fromHex("9000"));
        
        bool result = cmd.unblockPIN("123456789012", "654321");
        
        QVERIFY(result);
        QVERIFY(mock->getTransmitCount() > 0);
    }
    
    void testUnblockPINWrongPUK() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray pairingKey(32, 0xAB);
        PairingInfo pairingInfo(pairingKey, 1);
        QByteArray mockIV(16, 0x00);
        QByteArray mockEncKey(16, 0xEE);
        QByteArray mockMacKey(16, 0xDD);
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mock->simulateCardInserted();
        QTRY_VERIFY(channel->isConnected());
        
        mock->queueResponse(QByteArray::fromHex("63C5"));
        
        bool result = cmd.unblockPIN("000000000000", "654321");
        
        QVERIFY(!result);
        QVERIFY(cmd.lastError().contains("Wrong PUK"));
        QVERIFY(cmd.lastError().contains("5"));
    }
    
    void testChangePairingSecret() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray pairingKey(32, 0xAB);
        PairingInfo pairingInfo(pairingKey, 1);
        QByteArray mockIV(16, 0x00);
        QByteArray mockEncKey(16, 0xEE);
        QByteArray mockMacKey(16, 0xDD);
        cmd.testInjectSecureChannelState(pairingInfo, mockIV, mockEncKey, mockMacKey);
        
        mock->simulateCardInserted();
        QTRY_VERIFY(channel->isConnected());
        
        mock->queueResponse(QByteArray::fromHex("9000"));
        
        bool result = cmd.changePairingSecret("newpassword");
        
        QVERIFY(result);
    }
    
    void testGenerateKey() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.generateKey();
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
        QVERIFY(cmd.lastError().contains("APDU error") || cmd.lastError().contains("Secure channel"));
    }
    
    void testGenerateKeyWithoutSecureChannel() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.generateKey();
        
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testGenerateMnemonic() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QVector<int> result = cmd.generateMnemonic(4);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testGenerateMnemonicEmpty() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QVector<int> result = cmd.generateMnemonic();
        QVERIFY(result.isEmpty());
    }
    
    void testLoadSeed() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray seed(64, 0xAB);
        
        QByteArray result = cmd.loadSeed(seed);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testLoadSeedInvalidSize() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray seed(32, 0xAB);
        
        QByteArray result = cmd.loadSeed(seed);
        
        QVERIFY(result.isEmpty());
        QVERIFY(cmd.lastError().contains("64 bytes"));
    }
    
    void testRemoveKey() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.removeKey();
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testDeriveKeyAbsolutePath() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.deriveKey("m/44'/60'/0'/0/0");
        QVERIFY(!result);
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testDeriveKeyRelativePath() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.deriveKey("../0/1");
        QVERIFY(!result);
    }
    
    void testDeriveKeyCurrentPath() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.deriveKey("./5");
        QVERIFY(!result);
    }
    
    void testSign() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.sign(hash);
        QVERIFY(result.isEmpty());
        QVERIFY(!cmd.lastError().isEmpty());
    }
    
    void testSignInvalidHashSize() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray hash(16, 0x12);
        
        QByteArray result = cmd.sign(hash);
        
        QVERIFY(result.isEmpty());
        QVERIFY(cmd.lastError().contains("32 bytes"));
    }
    
    void testSignWithPath() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signWithPath(hash, "m/44'/60'/0'/0/0", false);
        QVERIFY(result.isEmpty());
    }
    
    void testSignWithPathMakeCurrent() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signWithPath(hash, "m/44'/60'/0'/0/0", true);
        QVERIFY(result.isEmpty());
    }
    
    void testSignPinless() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray hash(32, 0x12);
        QByteArray result = cmd.signPinless(hash);
        QVERIFY(result.isEmpty());
    }
    
    void testSetPinlessPath() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.setPinlessPath("m/44'/60'/0'/0/0");
        QVERIFY(!result);
    }
    
    void testSetPinlessPathRelative() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result = cmd.setPinlessPath("../0/0");
        QVERIFY(!result);
        QVERIFY(cmd.lastError().contains("absolute"));
    }
    
    void testStoreData() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray data = "Hello, Keycard!";
        bool result = cmd.storeData(0x00, data);
        QVERIFY(!result);
    }
    
    void testStoreDataNDEF() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray data = "NDEF data";
        bool result = cmd.storeData(0x01, data);
        QVERIFY(!result);
    }
    
    void testGetData() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.getData(0x00);
        QVERIFY(result.isEmpty());
    }
    
    void testGetDataEmpty() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.getData(0x00);
        QVERIFY(result.isEmpty());
    }
    
    void testIdentify() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray mockIdentity = "KeycardIdentity";
        mock->queueResponse(mockIdentity + QByteArray::fromHex("9000"));
        
        QByteArray result = cmd.identify();
        
        QVERIFY(!result.isEmpty());
        QCOMPARE(result, mockIdentity);
    }
    
    void testIdentifyWithChallenge() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray challenge(32, 0xAB);
        QByteArray mockIdentity = "KeycardIdentity";
        mock->queueResponse(mockIdentity + QByteArray::fromHex("9000"));
        
        QByteArray result = cmd.identify(challenge);
        
        QVERIFY(!result.isEmpty());
    }
    
    void testExportKeyCurrent() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.exportKey(false, false, "");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyDerive() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.exportKey(true, false, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyDeriveAndMakeCurrent() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.exportKey(true, true, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testExportKeyExtended() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray result = cmd.exportKeyExtended(true, false, "m/44'/60'/0'/0/0");
        QVERIFY(result.isEmpty());
    }
    
    void testFactoryReset() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        CommandSet cmd(channel, nullptr, nullptr);
        
        mock->queueResponse(QByteArray::fromHex("9000"));
        
        bool result = cmd.factoryReset();
        
        QVERIFY(result);
        QVERIFY(cmd.applicationInfo().instanceUID.isEmpty());
    }
    
    void testFactoryResetFailed() {
        auto channel = createMockChannel();
        auto* mock = qobject_cast<MockBackend*>(channel->backend());
        CommandSet cmd(channel, nullptr, nullptr);
        
        mock->queueResponse(QByteArray::fromHex("6985"));
        
        bool result = cmd.factoryReset();
        
        QVERIFY(!result);
    }
    
    void testMultipleOperationsSequence() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        QByteArray keyUID = cmd.generateKey();
        QVERIFY(keyUID.isEmpty());
        
        bool derived = cmd.deriveKey("m/44'/60'/0'/0/0");
        QVERIFY(!derived);
        
        QByteArray hash(32, 0x12);
        QByteArray sig = cmd.sign(hash);
        QVERIFY(sig.isEmpty());
    }
    
    void testPathParsingHardenedNotation() {
        auto channel = createMockChannel();
        CommandSet cmd(channel, nullptr, nullptr);
        
        bool result1 = cmd.deriveKey("m/44'/60'/0'");
        QVERIFY(!result1);
        
        bool result2 = cmd.deriveKey("m/44h/60h/0h");
        QVERIFY(!result2);
        
        QVERIFY(cmd.lastError().contains("APDU error") || cmd.lastError().contains("Secure channel"));
    }
    
    void cleanupTestCase() {
    }
};

QTEST_MAIN(TestCommandSetNew)
#include "test_command_set_new.moc"
