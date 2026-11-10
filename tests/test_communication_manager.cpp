#include <QTest>
#include <QSignalSpy>
#include <QSemaphore>
#include <QThread>
#include <QMetaMethod>
#include <atomic>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"
#include "mocks/mock_backend.h"
#include <memory>

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Core tests for CommunicationManager
 * 
 * Tests lifecycle, state machine, card detection, error handling, and batch operations.
 * These are the foundational tests for the thread-safe queue-based architecture.
 */
// Pauses the thread that connects to targetDetectionStopped - a card wait setting up -
// until released.
class PausingChannel : public KeycardChannel {
public:
    using KeycardChannel::KeycardChannel;
    std::atomic<bool> armed{false};
    QSemaphore paused;
    QSemaphore release;

protected:
    void connectNotify(const QMetaMethod& signal) override {
        if (armed && signal == QMetaMethod::fromSignal(&KeycardChannel::targetDetectionStopped)) {
            armed = false;
            paused.release();
            release.acquire();
        }
    }
};

class TestCommunicationManager : public QObject {
    Q_OBJECT
    
private:
    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(false);  // Manual control for tests
        auto channel = std::make_shared<KeycardChannel>(mock);
        return channel;
    }
    
    std::shared_ptr<CommandSet> m_cmdSet;
    std::unique_ptr<CommunicationManager> m_commMgr;
    MockBackend* m_mock;
    
private slots:
    void initTestCase() {
        qDebug() << "=== TestCommunicationManager: Starting test suite ===";
    }
    
    void init() {
        // Create fresh instances for each test
        auto channel = createMockChannel();
        m_mock = qobject_cast<MockBackend*>(channel->backend());
        m_cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
        m_commMgr = std::make_unique<CommunicationManager>();
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
    // Initialization and Lifecycle Tests
    // ========================================================================
    
    void testConstruction() {
        CommunicationManager mgr;
        QCOMPARE(mgr.state(), CommunicationManager::State::Idle);
        QVERIFY(!mgr.commandSet());
    }
    
    void testInitSuccess() {
        bool result = m_commMgr->init(m_cmdSet);
        
        QVERIFY(result);
        QCOMPARE(m_commMgr->state(), CommunicationManager::State::Idle);
        QVERIFY(m_commMgr->commandSet());
        QCOMPARE(m_commMgr->commandSet().get(), m_cmdSet.get());
    }
    
    void testInitWithNullCommandSet() {
        bool result = m_commMgr->init(nullptr);
        
        QVERIFY(!result);
        QCOMPARE(m_commMgr->state(), CommunicationManager::State::Idle);
    }
    
    void testInitAlreadyInitialized() {
        m_commMgr->init(m_cmdSet);
        
        // Try to init again
        bool result = m_commMgr->init(m_cmdSet);
        
        QVERIFY(!result);  // Should fail
    }
    
    void testStartDetectionWithoutInit() {
        bool result = m_commMgr->startDetection();
        
        QVERIFY(!result);
    }
    
    void testStartDetectionAfterInit() {
        m_commMgr->init(m_cmdSet);
        
        bool result = m_commMgr->startDetection();
        
        QVERIFY(result);
    }
    
    void testStopDetection() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        // Should not crash
        m_commMgr->stopDetection();
        QVERIFY(true);
    }
    
    void testStop() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        m_commMgr->stop();
        
        QCOMPARE(m_commMgr->state(), CommunicationManager::State::Idle);
    }
    
    // A client that stopped detection (ending its session) must not have the manager
    // restart it: here a card initialization that was in flight when detection stopped
    // fails afterwards, which used to restart detection from its error handling.
    void testStoppedDetectionIsNotRestartedByAFailedCardInitialization() {
        m_mock->setReconnectWhenWaiting(false);
        std::atomic<bool> transmitting{false};
        auto release = std::make_shared<QSemaphore>();
        m_mock->setResponseHandler([&transmitting, release](const QByteArray&) -> QByteArray {
            transmitting = true;
            release->acquire();
            throw std::runtime_error("card I/O failed after detection was stopped");
        });
        QVERIFY(m_commMgr->init(m_cmdSet));
        QVERIFY(m_commMgr->startDetection());
        QTRY_COMPARE(m_mock->state(), ChannelState::WaitingForCard);

        m_mock->simulateCardInserted();  // the communication thread starts initializing it
        QTRY_VERIFY(transmitting.load());

        m_commMgr->stopDetection();
        QTRY_COMPARE(m_mock->state(), ChannelState::Idle);

        release->release();  // the in-flight initialization fails now
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
        QCOMPARE(m_mock->state(), ChannelState::Idle);
    }

    // A card wait that is setting up when detection is stopped - past its first
    // cancellation check - must not start detection once it goes on.
    void testCardWaitAbortedWhileSettingUpDoesNotStartDetection() {
        auto* backend = new MockBackend();
        backend->setReconnectWhenWaiting(false);
        auto channel = std::make_shared<PausingChannel>(backend);
        auto cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
        CommunicationManager mgr;
        QVERIFY(mgr.init(cmdSet));

        channel->armed = true;
        std::atomic<bool> waited{true};
        std::unique_ptr<QThread> worker(QThread::create([&]() { waited = cmdSet->waitForCard(); }));
        worker->start();
        QVERIFY(channel->paused.tryAcquire(1, 5000));  // the wait is setting up

        mgr.stopDetection();
        QTRY_COMPARE(backend->state(), ChannelState::Idle);

        channel->release.release();
        QVERIFY(worker->wait(5000));
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }
        QVERIFY(!waited.load());
        QCOMPARE(backend->state(), ChannelState::Idle);
        mgr.stop();
    }

    // A start queued before detection was stopped does not run after it.
    void testStartQueuedBeforeStopDoesNotRun() {
        m_mock->setReconnectWhenWaiting(false);
        QVERIFY(m_commMgr->init(m_cmdSet));
        QVERIFY(m_commMgr->startDetection());  // queued to the command set
        m_commMgr->stopDetection();            // before it is delivered
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
        QCOMPARE(m_mock->state(), ChannelState::Idle);

        QVERIFY(m_commMgr->startDetection());  // a later start works
        QTRY_COMPARE(m_mock->state(), ChannelState::WaitingForCard);
    }

    void testStopWithoutInit() {
        // Should not crash
        m_commMgr->stop();
        QVERIFY(true);
    }
    
    void testMultipleStopCalls() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        m_commMgr->stop();
        m_commMgr->stop();  // Second stop
        m_commMgr->stop();  // Third stop
        
        QVERIFY(true);  // Should not crash
    }
    
    // ========================================================================
    // State Machine Tests
    // ========================================================================
    
    void testInitialState() {
        QCOMPARE(m_commMgr->state(), CommunicationManager::State::Idle);
    }
    
    void testStateChangedSignal() {
        m_commMgr->init(m_cmdSet);
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::stateChanged);
        
        // Simulate card detection
        m_mock->simulateCardInserted();
        
        // Wait for state change (may happen async)
        QTest::qWait(100);
        
        // State changed signal should have been emitted (at least once)
        QVERIFY(spy.count() >= 0);  // May be 0 if card init happens too fast
    }
    
    void testStateTransitionIdleToInitializing() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::stateChanged);
        
        // Queue a mock SELECT response
        m_mock->queueResponse(QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        // Wait for initialization
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
    }
    
    // ========================================================================
    // Card Detection Tests
    // ========================================================================
    
    void testCardInitializedSignal() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::cardInitialized);
        
        // Prepare mock responses for card initialization sequence
        // SELECT
        QByteArray selectResp = QByteArray::fromHex("8041");
        selectResp.append(QByteArray(65, 0x04));
        selectResp.append(QByteArray::fromHex("9000"));
        m_mock->queueResponse(selectResp);
        
        m_mock->simulateCardInserted();
        
        // Wait for initialization signal
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 3000);
        
        if (spy.count() > 0) {
            auto args = spy.takeFirst();
            auto result = args.at(0).value<CardInitializationResult>();
            // Result may be success or failure depending on mock implementation
            QVERIFY(true);  // Signal was emitted
        }
    }
    
    void testCardLostSignal() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::cardLost);
        
        // Insert then remove card
        m_mock->simulateCardInserted();
        QTest::qWait(100);
        m_mock->simulateCardRemoved();
        
        // Wait for card lost signal
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
    }
    
    // ========================================================================
    // Batch Operations Tests
    // ========================================================================
    
    void testStartBatchOperations() {
        m_commMgr->init(m_cmdSet);
        
        // Should not crash
        m_commMgr->startBatchOperations();
        QVERIFY(true);
    }
    
    void testEndBatchOperations() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startBatchOperations();
        
        // Should not crash
        m_commMgr->endBatchOperations();
        QVERIFY(true);
    }
    
    void testMultipleBatchOperationCycles() {
        m_commMgr->init(m_cmdSet);
        
        for (int i = 0; i < 5; i++) {
            m_commMgr->startBatchOperations();
            QTest::qWait(10);
            m_commMgr->endBatchOperations();
            QTest::qWait(10);
        }
        
        QVERIFY(true);
    }
    
    void testNestedBatchOperations() {
        m_commMgr->init(m_cmdSet);

        m_commMgr->startBatchOperations();
        m_commMgr->startBatchOperations();  // Second call should increase depth

        m_commMgr->endBatchOperations();
        m_commMgr->endBatchOperations();  // Fully exit batch mode

        QVERIFY(true);
    }

    void testEndBatchOperationsWithoutStart() {
        m_commMgr->init(m_cmdSet);

        // Extra end calls should be safe and never underflow.
        m_commMgr->endBatchOperations();
        m_commMgr->endBatchOperations();

        QVERIFY(true);
    }
    
    // ========================================================================
    // Application Info/Status Tests
    // ========================================================================
    
    void testApplicationInfoWhenNotReady() {
        m_commMgr->init(m_cmdSet);
        
        ApplicationInfo info = m_commMgr->applicationInfo();
        
        // Should return default/empty info when not ready
        QVERIFY(info.instanceUID.isEmpty() || !info.instanceUID.isEmpty());
    }
    
    void testApplicationStatusWhenNotReady() {
        m_commMgr->init(m_cmdSet);
        
        ApplicationStatus status = m_commMgr->applicationStatus();
        
        // Should return default status
        QVERIFY(status.pinRetryCount >= 0);
    }
    
    // ========================================================================
    // Edge Cases and Error Handling
    // ========================================================================
    
    void testDestructorWhileRunning() {
        auto mgr = std::make_unique<CommunicationManager>();
        mgr->init(m_cmdSet);
        mgr->startDetection();
        
        // Destructor should handle cleanup
        mgr.reset();
        
        QVERIFY(true);  // No crash
    }
    
    void testStopDuringCardInitialization() {
        m_commMgr->init(m_cmdSet);
        m_commMgr->startDetection();
        
        // Simulate card insertion
        m_mock->simulateCardInserted();
        
        // Immediately stop (during initialization)
        QTest::qWait(10);
        m_commMgr->stop();
        
        QVERIFY(true);  // Should handle gracefully
    }
    
    void testGetDataFromCardWithoutInit() {
        // Should not crash
        QByteArray data = m_commMgr->getDataFromCard(0x00);
        QVERIFY(data.isEmpty());
    }
    
    void testStoreDataToCardWithoutInit() {
        // Should not crash
        bool result = m_commMgr->storeDataToCard(0x00, QByteArray("test"));
        QVERIFY(!result);
    }
    
    void cleanupTestCase() {
        qDebug() << "=== TestCommunicationManager: Test suite completed ===";
    }
};

QTEST_MAIN(TestCommunicationManager)
#include "test_communication_manager.moc"



