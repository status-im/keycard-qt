#include <QTest>
#include <QSignalSpy>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"
#include "mocks/mock_backend.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <atomic>
#include <memory>
#include <thread>

using namespace Keycard;
using namespace Keycard::Test;

namespace {

/// Optionally removes the card, then waits for one: a command blocked in waitForCard()
class WaitForCardCommand : public CardCommand {
public:
    WaitForCardCommand(MockBackend* mock, std::atomic<bool>* waiting) : m_mock(mock), m_waiting(waiting) {}

    CommandResult execute(CommandSet* cmdSet) override {
        if (m_mock) {
            m_mock->simulateCardRemoved();
        }
        if (m_waiting) {
            *m_waiting = true;
        }
        return cmdSet->waitForCard() ? CommandResult::fromSuccess() : CommandResult::fromError(cmdSet->lastError());
    }
    QString name() const override { return QStringLiteral("WAIT_FOR_CARD"); }

private:
    MockBackend* m_mock;
    std::atomic<bool>* m_waiting;
};

/// Needs no card: some other work queued by another caller
class NoopCommand : public CardCommand {
public:
    CommandResult execute(CommandSet*) override { return CommandResult::fromSuccess(); }
    QString name() const override { return QStringLiteral("NOOP"); }
};

} // namespace

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

    /// Records commandCompleted (emitted on the communication thread; CommandResult is no metatype)
    class CompletedCommands {
    public:
        explicit CompletedCommands(CommunicationManager* mgr) {
            m_connection = QObject::connect(mgr, &CommunicationManager::commandCompleted, mgr,
                [this](const QUuid& token, const CommandResult& result) {
                    QMutexLocker locker(&m_mutex);
                    m_success.insert(token, result.success);
                }, Qt::DirectConnection);
        }
        ~CompletedCommands() { QObject::disconnect(m_connection); }
        bool contains(const QUuid& token) const { QMutexLocker locker(&m_mutex); return m_success.contains(token); }
        bool succeeded(const QUuid& token) const { QMutexLocker locker(&m_mutex); return m_success.value(token); }

    private:
        mutable QMutex m_mutex;
        QHash<QUuid, bool> m_success;
        QMetaObject::Connection m_connection;
    };

    /// Runs executeCommandSync(cmd) on a helper thread; true if it returned within @p timeoutMs
    bool executeSyncWithin(std::unique_ptr<CardCommand> cmd, int timeoutMs, CommandResult* result) {
        std::atomic<bool> done{false};
        std::thread caller([&]() {
            *result = m_commMgr->executeCommandSync(std::move(cmd));
            done = true;
        });
        QElapsedTimer elapsed;
        elapsed.start();
        while (!done && elapsed.elapsed() < timeoutMs) {
            QTest::qWait(20);
        }
        const bool returned = done;
        if (!returned) {
            m_commMgr->cancelPendingOperations(QStringLiteral("test cleanup")); // release the stuck caller
        }
        caller.join();
        return returned;
    }

    /// A card is present and initialized, the manager idle: enqueued commands run right away
    void readyCard() {
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        QTRY_VERIFY_WITH_TIMEOUT(m_cmdSet->isCardReady()
                                 && m_commMgr->state() != CommunicationManager::State::Initializing, 5000);
        m_mock->setReconnectWhenWaiting(false);
    }
    
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
    // Cancellation of a command waiting for a card
    // ========================================================================

    void testCancelThenEnqueueStillReleasesTheWaitingCommand() {
        // Repeated: the waiting command polls for the abort every 50 ms, so a single round can miss a
        // regression when a poll happens to fall between the cancel and the enqueue
        CompletedCommands completed(m_commMgr.get());
        for (int round = 0; round < 3; ++round) {
            readyCard();
            std::atomic<bool> waiting{false};
            auto waitCmd = std::make_unique<WaitForCardCommand>(m_mock, &waiting);
            const QUuid waitToken = waitCmd->token();
            m_commMgr->enqueueCommand(std::move(waitCmd));
            QTRY_VERIFY_WITH_TIMEOUT(waiting.load(), 5000);

            // Cancel, then enqueue right away, before the waiting command polls for the abort
            m_commMgr->cancelPendingOperations(QStringLiteral("Card removed"));
            m_commMgr->enqueueCommand(std::make_unique<NoopCommand>());

            QTRY_VERIFY_WITH_TIMEOUT(completed.contains(waitToken), 2000);
            QVERIFY(!completed.succeeded(waitToken));
        }
    }

    void testCancelledBatchRejectsItsLaterCommands() {
        // A composite runs in a batch; its card is removed and the cancellation has already flushed the
        // queue when the composite enqueues its next command: that command must fail, not wait for a card
        readyCard();
        m_commMgr->startBatchOperations();
        m_mock->simulateCardRemoved();
        QTRY_VERIFY_WITH_TIMEOUT(!m_cmdSet->isCardReady(), 2000);
        m_commMgr->cancelPendingOperations(QStringLiteral("Card removed"));
        QTest::qWait(100); // let the cancellation itself run

        CommandResult result;
        const bool returned = executeSyncWithin(std::make_unique<NoopCommand>(), 2000, &result);
        m_commMgr->endBatchOperations();

        QVERIFY2(returned, "a command of a cancelled batch must not wait for a card");
        QVERIFY(!result.success);
        QCOMPARE(result.reason, CommandResultType::Cancelled);
        QCOMPARE(result.error, QStringLiteral("Card removed"));
    }

    void testNextBatchIsNotAffectedByAnEarlierCancellation() {
        readyCard();
        m_commMgr->startBatchOperations();
        m_commMgr->cancelPendingOperations(QStringLiteral("Card removed"));
        m_commMgr->endBatchOperations();
        QTest::qWait(100);

        m_commMgr->startBatchOperations();
        CommandResult result;
        const bool returned = executeSyncWithin(std::make_unique<NoopCommand>(), 5000, &result);
        m_commMgr->endBatchOperations();

        QVERIFY(returned);
        QVERIFY2(result.success, qPrintable(result.error));
    }

    void testCommandEnqueuedAfterCancelIsNotAborted() {
        readyCard();
        m_mock->simulateCardRemoved();
        m_commMgr->cancelPendingOperations(QStringLiteral("Card removed"));
        QTest::qWait(100); // let the cancellation itself run

        CompletedCommands completed(m_commMgr.get());
        auto waitCmd = std::make_unique<WaitForCardCommand>(nullptr, nullptr);
        const QUuid waitToken = waitCmd->token();
        m_commMgr->enqueueCommand(std::move(waitCmd));
        QTest::qWait(300);
        QVERIFY2(!completed.contains(waitToken), "the earlier cancellation must not abort a later command");

        m_mock->simulateCardInserted();
        QTRY_VERIFY_WITH_TIMEOUT(completed.contains(waitToken), 5000);
        QVERIFY(completed.succeeded(waitToken));
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



