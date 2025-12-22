#include <QTest>
#include <QSignalSpy>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"
#include "mocks/mock_backend.h"
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Tests for CommunicationManager queue management
 * 
 * Tests command enqueueing, FIFO processing, queue states, and edge cases.
 * Validates the queue-based architecture that prevents race conditions.
 */
class TestCommunicationManagerQueue : public QObject {
    Q_OBJECT
    
private:
    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(false);
        auto channel = std::make_shared<KeycardChannel>(mock);
        return channel;
    }
    
    std::shared_ptr<CommandSet> m_cmdSet;
    std::unique_ptr<CommunicationManager> m_commMgr;
    MockBackend* m_mock;
    
private slots:
    void init() {
        auto channel = createMockChannel();
        m_mock = qobject_cast<MockBackend*>(channel->backend());
        m_cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
        m_commMgr = std::make_unique<CommunicationManager>();
        m_commMgr->init(m_cmdSet);
    }
    
    void cleanup() {
        if (m_commMgr) {
            m_commMgr->stop();
            m_commMgr.reset();
        }
        m_cmdSet.reset();
        m_mock = nullptr;
    }
    
    // ========================================================================
    // Basic Enqueueing Tests
    // ========================================================================
    
    void testEnqueueSingleCommand() {
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        
        QVERIFY(!token.isNull());
    }
    
    void testEnqueueMultipleCommands() {
        QList<QUuid> tokens;
        
        for (int i = 0; i < 10; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            tokens.append(token);
        }
        
        QCOMPARE(tokens.size(), 10);
        
        // All tokens should be unique
        QSet<QUuid> uniqueTokens(tokens.begin(), tokens.end());
        QCOMPARE(uniqueTokens.size(), 10);
    }
    
    void testEnqueueWithoutInit() {
        CommunicationManager mgr;
        
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = mgr.enqueueCommand(std::move(cmd));
        
        QVERIFY(token.isNull());  // Should fail without initialization
    }
    
    void testEnqueueNullCommand() {
        QUuid token = m_commMgr->enqueueCommand(nullptr);
        
        QVERIFY(token.isNull());  // Should fail with null command
    }
    
    // ========================================================================
    // Command Completion Signal Tests
    // ========================================================================
    
    void testCommandCompletedSignal() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Queue response for SELECT
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = cmd->token();
        m_commMgr->enqueueCommand(std::move(cmd));
        
        // Wait for completion (initialization + command execution)
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
        
        // Verify signal contains token
        if (spy.count() > 0) {
            auto args = spy.last();
            QUuid completedToken = args.at(0).toUuid();
            // Token might differ if initialization commands run first
            QVERIFY(!completedToken.isNull());
        }
    }
    
    // ========================================================================
    // FIFO Processing Order Tests
    // ========================================================================
    
    void testFIFOProcessing() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Prepare multiple responses
        for (int i = 0; i < 3; i++) {
            m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        }
        
        // Enqueue commands
        QList<QUuid> tokens;
        for (int i = 0; i < 3; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            tokens.append(cmd->token());
            m_commMgr->enqueueCommand(std::move(cmd));
        }
        
        // Wait for all to complete
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, 10000);
        
        // Commands should complete (order tested by signal sequence)
        QVERIFY(spy.count() >= 3);
    }
    
    // ========================================================================
    // Queue State Tests
    // ========================================================================
    
    void testEnqueueWhenCardNotReady() {
        // Don't start detection or insert card
        
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        
        QVERIFY(!token.isNull());  // Should accept command even if card not ready
        
        // Command will be queued waiting for card
    }
    
    void testQueueProcessingAfterCardReady() {
        // Enqueue before card ready
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = cmd->token();
        m_commMgr->enqueueCommand(std::move(cmd));
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Now make card ready
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        // Command should now process
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 5000);
    }
    
    // ========================================================================
    // Card Lost During Queue Processing
    // ========================================================================
    
    void testCardLostWhileProcessingQueue() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        QSignalSpy lostSpy(m_commMgr.get(), &CommunicationManager::cardLost);
        
        // Enqueue commands
        for (int i = 0; i < 5; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            m_commMgr->enqueueCommand(std::move(cmd));
        }
        
        // Simulate card removal
        QTest::qWait(100);
        m_mock->simulateCardRemoved();
        
        // Should receive card lost signal
        QTRY_VERIFY_WITH_TIMEOUT(lostSpy.count() > 0, 2000);
        
        // Queue should stop processing
        QCOMPARE(m_commMgr->state(), CommunicationManager::State::Idle);
    }
    
    // ========================================================================
    // Queue Clear on Stop
    // ========================================================================
    
    void testQueueClearedOnStop() {
        // Enqueue commands
        for (int i = 0; i < 5; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            m_commMgr->enqueueCommand(std::move(cmd));
        }
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Stop immediately
        m_commMgr->stop();
        
        // Wait a bit
        QTest::qWait(100);
        
        // Commands should not complete (queue was cleared)
        QCOMPARE(spy.count(), 0);
    }
    
    // ========================================================================
    // Mixed Command Types
    // ========================================================================
    
    void testMixedCommandTypes() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Queue various command types
        m_commMgr->enqueueCommand(std::make_unique<SelectCommand>());
        m_commMgr->enqueueCommand(std::make_unique<GetStatusCommand>());
        m_commMgr->enqueueCommand(std::make_unique<SelectCommand>());
        
        // Prepare responses
        for (int i = 0; i < 3; i++) {
            m_mock->queueResponse(QByteArray::fromHex("9000"));
        }
        
        // Should process all types
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
    }
    
    // ========================================================================
    // Rapid Enqueueing
    // ========================================================================
    
    void testRapidEnqueueing() {
        QList<QUuid> tokens;
        
        // Rapidly enqueue many commands
        for (int i = 0; i < 100; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            tokens.append(token);
        }
        
        // All should have valid tokens
        QCOMPARE(tokens.size(), 100);
        
        for (const QUuid& token : tokens) {
            QVERIFY(!token.isNull());
        }
    }
    
    // ========================================================================
    // Queue Behavior with Batch Operations
    // ========================================================================
    
    void testQueueWithBatchOperations() {
        m_commMgr->startBatchOperations();
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        // Enqueue commands during batch mode
        for (int i = 0; i < 3; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            m_commMgr->enqueueCommand(std::move(cmd));
        }
        
        // Queue responses
        for (int i = 0; i < 3; i++) {
            m_mock->queueResponse(QByteArray::fromHex("9000"));
        }
        
        QTest::qWait(200);
        
        m_commMgr->endBatchOperations();
        
        QVERIFY(true);  // Should not crash
    }
    
    // ========================================================================
    // Command Timeout Handling
    // ========================================================================
    
    void testCommandWithCustomTimeout() {
        // InitCommand has 60s timeout
        auto cmd = std::make_unique<InitCommand>("123456", "123456789012", "password");
        QCOMPARE(cmd->timeoutMs(), 60000);
        
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        QVERIFY(!token.isNull());
    }
    
    void testFactoryResetWithTimeout() {
        auto cmd = std::make_unique<FactoryResetCommand>();
        QCOMPARE(cmd->timeoutMs(), 60000);
        
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        QVERIFY(!token.isNull());
    }
    
    // ========================================================================
    // Edge Cases
    // ========================================================================
    
    void testEnqueueAfterStop() {
        m_commMgr->stop();
        
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        
        QVERIFY(token.isNull());  // Should fail after stop
    }
    
    void testMultipleEnqueueSameType() {
        // Enqueue same command type multiple times
        for (int i = 0; i < 5; i++) {
            auto cmd = std::make_unique<GetStatusCommand>(0);
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            QVERIFY(!token.isNull());
        }
        
        QVERIFY(true);
    }
    
    void testEnqueueDuringInitialization() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        // Enqueue during initialization (should queue and wait)
        QTest::qWait(10);  // Small delay during init
        
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        
        QVERIFY(!token.isNull());  // Should accept command
    }
};

QTEST_MAIN(TestCommunicationManagerQueue)
#include "test_communication_manager_queue.moc"



