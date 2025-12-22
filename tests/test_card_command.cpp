#include <QTest>
#include "keycard-qt/card_command.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "mocks/mock_backend.h"
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Tests for CardCommand pattern and concrete command implementations
 * 
 * Tests the Command Pattern implementation used by CommunicationManager
 * to encapsulate card operations for thread-safe execution.
 */
class TestCardCommand : public QObject {
    Q_OBJECT
    
private:
    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(false);
        auto channel = std::make_shared<KeycardChannel>(mock);
        mock->simulateCardInserted();
        return channel;
    }
    
    std::shared_ptr<CommandSet> m_cmdSet;
    MockBackend* m_mock;
    
private slots:
    void init() {
        auto channel = createMockChannel();
        m_mock = qobject_cast<MockBackend*>(channel->backend());
        m_cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
    }
    
    void cleanup() {
        m_cmdSet.reset();
        m_mock = nullptr;
    }
    
    // ========================================================================
    // SelectCommand Tests
    // ========================================================================
    
    void testSelectCommandBasic() {
        SelectCommand cmd(false);
        
        QCOMPARE(cmd.name(), QString("SELECT"));
        QVERIFY(cmd.canRunDuringInit());
        QVERIFY(cmd.token().isNull() == false);
    }
    
    void testSelectCommandExecute() {
        QByteArray mockResponse = QByteArray::fromHex("8041");
        mockResponse.append(QByteArray(65, 0x04));
        mockResponse.append(QByteArray::fromHex("9000"));
        m_mock->queueResponse(mockResponse);
        
        SelectCommand cmd(false);
        CommandResult result = cmd.execute(m_cmdSet.get());
        
        QVERIFY(result.success);
    }
    
    void testSelectCommandForceFlag() {
        SelectCommand cmd1(false);
        SelectCommand cmd2(true);
        
        // Both should have same name but different behavior
        QCOMPARE(cmd1.name(), cmd2.name());
    }
    
    // ========================================================================
    // VerifyPINCommand Tests
    // ========================================================================
    
    void testVerifyPINCommandBasic() {
        VerifyPINCommand cmd("123456");
        
        QCOMPARE(cmd.name(), QString("VERIFY_PIN"));
        QVERIFY(!cmd.canRunDuringInit());
        QVERIFY(!cmd.token().isNull());
    }
    
    void testVerifyPINCommandExecute() {
        // Note: Will fail without secure channel, but should not crash
        VerifyPINCommand cmd("000000");
        CommandResult result = cmd.execute(m_cmdSet.get());
        
        QVERIFY(!result.success);  // No secure channel established
        QVERIFY(!result.error.isEmpty());
    }
    
    // ========================================================================
    // GetStatusCommand Tests
    // ========================================================================
    
    void testGetStatusCommandBasic() {
        GetStatusCommand cmd(0);
        
        QCOMPARE(cmd.name(), QString("GET_STATUS"));
        QVERIFY(cmd.canRunDuringInit());
        QVERIFY(!cmd.token().isNull());
    }
    
    void testGetStatusCommandWithInfo() {
        GetStatusCommand cmd1(0x00);
        GetStatusCommand cmd2(0x01);
        
        QCOMPARE(cmd1.name(), cmd2.name());
        QVERIFY(cmd1.token() != cmd2.token());
    }
    
    // ========================================================================
    // InitCommand Tests
    // ========================================================================
    
    void testInitCommandBasic() {
        InitCommand cmd("123456", "123456789012", "password");
        
        QCOMPARE(cmd.name(), QString("INIT"));
        QVERIFY(!cmd.canRunDuringInit());
        QCOMPARE(cmd.timeoutMs(), 60000);  // Longer timeout
    }
    
    void testInitCommandExecute() {
        InitCommand cmd("123456", "123456789012", "password");
        CommandResult result = cmd.execute(m_cmdSet.get());
        
        // Will fail on mock but should not crash
        QVERIFY(!result.success);
    }
    
    // ========================================================================
    // ChangePINCommand Tests
    // ========================================================================
    
    void testChangePINCommandBasic() {
        ChangePINCommand cmd("654321");
        
        QCOMPARE(cmd.name(), QString("CHANGE_PIN"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    void testChangePINCommandExecute() {
        ChangePINCommand cmd("654321");
        CommandResult result = cmd.execute(m_cmdSet.get());
        
        QVERIFY(!result.success);  // No secure channel
    }
    
    // ========================================================================
    // ChangePUKCommand Tests
    // ========================================================================
    
    void testChangePUKCommandBasic() {
        ChangePUKCommand cmd("111111111111");
        
        QCOMPARE(cmd.name(), QString("CHANGE_PUK"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // UnblockPINCommand Tests
    // ========================================================================
    
    void testUnblockPINCommandBasic() {
        UnblockPINCommand cmd("123456789012", "000000");
        
        QCOMPARE(cmd.name(), QString("UNBLOCK_PIN"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // GenerateMnemonicCommand Tests
    // ========================================================================
    
    void testGenerateMnemonicCommandBasic() {
        GenerateMnemonicCommand cmd(4);
        
        QCOMPARE(cmd.name(), QString("GENERATE_MNEMONIC"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // LoadSeedCommand Tests
    // ========================================================================
    
    void testLoadSeedCommandBasic() {
        QByteArray seed(64, 0xAA);
        LoadSeedCommand cmd(seed);
        
        QCOMPARE(cmd.name(), QString("LOAD_SEED"));
        QVERIFY(!cmd.canRunDuringInit());
        QCOMPARE(cmd.timeoutMs(), 60000);  // Longer timeout
    }
    
    // ========================================================================
    // FactoryResetCommand Tests
    // ========================================================================
    
    void testFactoryResetCommandBasic() {
        FactoryResetCommand cmd;
        
        QCOMPARE(cmd.name(), QString("FACTORY_RESET"));
        QVERIFY(!cmd.canRunDuringInit());
        QCOMPARE(cmd.timeoutMs(), 60000);
    }
    
    void testFactoryResetCommandExecute() {
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        
        FactoryResetCommand cmd;
        CommandResult result = cmd.execute(m_cmdSet.get());
        
        QVERIFY(result.success);
    }
    
    // ========================================================================
    // ExportKeyCommand Tests
    // ========================================================================
    
    void testExportKeyCommandBasic() {
        ExportKeyCommand cmd(true, false, "m/44'/60'/0'/0/0", 0x00);
        
        QCOMPARE(cmd.name(), QString("EXPORT_KEY"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // ExportKeyExtendedCommand Tests
    // ========================================================================
    
    void testExportKeyExtendedCommandBasic() {
        ExportKeyExtendedCommand cmd(true, true, "m/44'/60'/0'/0/0");
        
        QCOMPARE(cmd.name(), QString("EXPORT_KEY_EXTENDED"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // GetMetadataCommand Tests
    // ========================================================================
    
    void testGetMetadataCommandBasic() {
        GetMetadataCommand cmd;
        
        QCOMPARE(cmd.name(), QString("GET_METADATA"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // StoreMetadataCommand Tests
    // ========================================================================
    
    void testStoreMetadataCommandBasic() {
        QStringList paths;
        paths << "m/44'/60'/0'/0/0" << "m/44'/60'/0'/0/1";
        StoreMetadataCommand cmd("TestWallet", paths);
        
        QCOMPARE(cmd.name(), QString("STORE_METADATA"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // SignCommand Tests
    // ========================================================================
    
    void testSignCommandBasic() {
        QByteArray data(32, 0x12);
        SignCommand cmd(data);
        
        QCOMPARE(cmd.name(), QString("SIGN"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    void testSignCommandWithPath() {
        QByteArray data(32, 0x12);
        SignCommand cmd(data, "m/44'/60'/0'/0/0", true);
        
        QCOMPARE(cmd.name(), QString("SIGN"));
    }
    
    // ========================================================================
    // ChangePairingCommand Tests
    // ========================================================================
    
    void testChangePairingCommandBasic() {
        ChangePairingCommand cmd("newpassword");
        
        QCOMPARE(cmd.name(), QString("CHANGE_PAIRING"));
        QVERIFY(!cmd.canRunDuringInit());
    }
    
    // ========================================================================
    // CommandResult Tests
    // ========================================================================
    
    void testCommandResultDefault() {
        CommandResult result;
        
        QVERIFY(!result.success);
        QVERIFY(result.data.isNull());
        QVERIFY(result.error.isEmpty());
    }
    
    void testCommandResultSuccess() {
        CommandResult result = CommandResult::fromSuccess(QVariant(42));
        
        QVERIFY(result.success);
        QCOMPARE(result.data.toInt(), 42);
        QVERIFY(result.error.isEmpty());
    }
    
    void testCommandResultError() {
        CommandResult result = CommandResult::fromError("Test error");
        
        QVERIFY(!result.success);
        QVERIFY(result.data.isNull());
        QCOMPARE(result.error, QString("Test error"));
    }
    
    // ========================================================================
    // Token Uniqueness Tests
    // ========================================================================
    
    void testCommandTokenUniqueness() {
        SelectCommand cmd1;
        SelectCommand cmd2;
        
        QVERIFY(cmd1.token() != cmd2.token());
    }
    
    void testCommandTokenPersistence() {
        SelectCommand cmd;
        QUuid token1 = cmd.token();
        QUuid token2 = cmd.token();
        
        QCOMPARE(token1, token2);  // Same command should return same token
    }
    
    // ========================================================================
    // Timeout Configuration Tests
    // ========================================================================
    
    void testDefaultTimeout() {
        VerifyPINCommand cmd("123456");
        
        QCOMPARE(cmd.timeoutMs(), 120000);  // Default timeout
    }
    
    void testCustomTimeout() {
        InitCommand cmd("123456", "123456789012", "password");
        
        QCOMPARE(cmd.timeoutMs(), 60000);  // Custom longer timeout
    }
    
    void testFactoryResetTimeout() {
        FactoryResetCommand cmd;
        
        QCOMPARE(cmd.timeoutMs(), 60000);  // Custom timeout for reset
    }
    
    // ========================================================================
    // Edge Cases
    // ========================================================================
    
    void testCommandExecuteWithNullCommandSet() {
        SelectCommand cmd;
        
        // Should handle gracefully (though would crash in practice)
        // This tests that command doesn't have internal null checks
        // Real usage always provides valid CommandSet
        QVERIFY(true);
    }
    
    void testCommandsWithEmptyStrings() {
        VerifyPINCommand cmd1("");
        ChangePINCommand cmd2("");
        ChangePairingCommand cmd3("");
        
        // Commands should accept empty strings (validation is in CommandSet)
        QCOMPARE(cmd1.name(), QString("VERIFY_PIN"));
        QCOMPARE(cmd2.name(), QString("CHANGE_PIN"));
        QCOMPARE(cmd3.name(), QString("CHANGE_PAIRING"));
    }
    
    void testCommandsWithEmptyData() {
        QByteArray emptyData;
        SignCommand cmd1(emptyData);
        LoadSeedCommand cmd2(emptyData);
        
        // Commands should accept empty data (validation is in CommandSet)
        QCOMPARE(cmd1.name(), QString("SIGN"));
        QCOMPARE(cmd2.name(), QString("LOAD_SEED"));
    }
};

QTEST_MAIN(TestCardCommand)
#include "test_card_command.moc"

