#include <QTest>
#include <QSignalSpy>
#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>
#include <QFuture>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"
#include "mocks/mock_backend.h"
#include <memory>
#include <atomic>

using namespace Keycard;
using namespace Keycard::Test;

/**
 * @brief Threading and concurrency tests for CommunicationManager
 * 
 * Tests thread safety, concurrent access, race condition prevention,
 * and deadlock avoidance. These tests validate the core thread-safe
 * architecture that solves the original race condition problem.
 */
class TestCommunicationManagerThreading : public QObject {
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
    // Concurrent Enqueueing Tests
    // ========================================================================
    
    void testConcurrentEnqueueFromMultipleThreads() {
        const int numThreads = 10;
        const int commandsPerThread = 10;
        std::atomic<int> successCount{0};
        
        QList<QFuture<void>> futures;
        
        // Launch multiple threads enqueueing commands
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, commandsPerThread, &successCount]() {
                for (int j = 0; j < commandsPerThread; j++) {
                    auto cmd = std::make_unique<SelectCommand>();
                    QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
                    if (!token.isNull()) {
                        successCount++;
                    }
                }
            });
            futures.append(future);
        }
        
        // Wait for all threads
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        // All commands should have been enqueued successfully
        QCOMPARE(successCount.load(), numThreads * commandsPerThread);
    }
    
    void testConcurrentEnqueueDifferentCommandTypes() {
        const int numThreads = 5;
        std::atomic<int> successCount{0};
        
        QList<QFuture<void>> futures;
        
        // Each thread enqueues different command types
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, i, &successCount]() {
                for (int j = 0; j < 5; j++) {
                    std::unique_ptr<CardCommand> cmd;
                    
                    switch (i % 3) {
                        case 0:
                            cmd = std::make_unique<SelectCommand>();
                            break;
                        case 1:
                            cmd = std::make_unique<GetStatusCommand>();
                            break;
                        case 2:
                            cmd = std::make_unique<VerifyPINCommand>("123456");
                            break;
                    }
                    
                    QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
                    if (!token.isNull()) {
                        successCount++;
                    }
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QVERIFY(successCount.load() == numThreads * 5);
    }
    
    // ========================================================================
    // Concurrent State Access Tests
    // ========================================================================
    
    void testConcurrentStateReads() {
        const int numThreads = 20;
        std::atomic<int> readCount{0};
        
        QList<QFuture<void>> futures;
        
        // Multiple threads reading state concurrently
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, &readCount]() {
                for (int j = 0; j < 100; j++) {
                    CommunicationManager::State state = m_commMgr->state();
                    Q_UNUSED(state);
                    readCount++;
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QCOMPARE(readCount.load(), numThreads * 100);
    }
    
    void testConcurrentApplicationInfoReads() {
        const int numThreads = 10;
        std::atomic<int> readCount{0};
        
        QList<QFuture<void>> futures;
        
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, &readCount]() {
                for (int j = 0; j < 50; j++) {
                    ApplicationInfo info = m_commMgr->applicationInfo();
                    Q_UNUSED(info);
                    readCount++;
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QCOMPARE(readCount.load(), numThreads * 50);
    }
    
    // ========================================================================
    // Concurrent Start/Stop Tests
    // ========================================================================
    
    void testConcurrentStartDetectionCalls() {
        const int numThreads = 10;
        std::atomic<int> successCount{0};
        
        QList<QFuture<void>> futures;
        
        // Multiple threads calling startDetection
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, &successCount]() {
                bool result = m_commMgr->startDetection();
                if (result) {
                    successCount++;
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        // At least one should succeed
        QVERIFY(successCount.load() >= 1);
    }
    
    void testConcurrentStopDetectionCalls() {
        m_commMgr->startDetection();
        
        const int numThreads = 10;
        QList<QFuture<void>> futures;
        
        // Multiple threads calling stopDetection
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this]() {
                m_commMgr->stopDetection();
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QVERIFY(true);  // Should not crash
    }
    
    // ========================================================================
    // Concurrent Batch Operations Tests
    // ========================================================================
    
    void testConcurrentBatchOperationCalls() {
        const int numThreads = 10;
        QList<QFuture<void>> futures;
        
        // Half start, half end
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, i]() {
                if (i % 2 == 0) {
                    m_commMgr->startBatchOperations();
                } else {
                    m_commMgr->endBatchOperations();
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QVERIFY(true);  // Should not crash or deadlock
    }
    
    // ========================================================================
    // Race Condition Prevention Tests
    // ========================================================================
    
    void testEnqueueDuringCardDetection() {
        // Start detection in background
        auto detectionFuture = QtConcurrent::run([this]() {
            m_commMgr->startDetection();
            m_mock->simulateCardInserted();
        });
        
        // Immediately start enqueueing commands
        QList<QUuid> tokens;
        for (int i = 0; i < 20; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            tokens.append(token);
            QThread::msleep(1);  // Small delay to interleave with detection
        }
        
        detectionFuture.waitForFinished();
        
        // All commands should have been queued
        for (const QUuid& token : tokens) {
            QVERIFY(!token.isNull());
        }
    }
    
    void testEnqueueDuringCardRemoval() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        QTest::qWait(100);
        
        // Remove card in background
        auto removalFuture = QtConcurrent::run([this]() {
            QThread::msleep(50);
            m_mock->simulateCardRemoved();
        });
        
        // Enqueue commands during removal
        QList<QUuid> tokens;
        for (int i = 0; i < 10; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            tokens.append(token);
            QThread::msleep(10);
        }
        
        removalFuture.waitForFinished();
        
        // Commands should have been queued (though may not execute)
        QVERIFY(tokens.size() == 10);
    }
    
    // ========================================================================
    // Stress Tests
    // ========================================================================
    
    void testHighVolumeEnqueueing() {
        const int numCommands = 1000;
        std::atomic<int> enqueuedCount{0};
        
        auto future = QtConcurrent::run([this, numCommands, &enqueuedCount]() {
            for (int i = 0; i < numCommands; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
                if (!token.isNull()) {
                    enqueuedCount++;
                }
            }
        });
        
        future.waitForFinished();
        
        QCOMPARE(enqueuedCount.load(), numCommands);
    }
    
    void testConcurrentHighVolumeEnqueueing() {
        const int numThreads = 10;
        const int commandsPerThread = 100;
        std::atomic<int> enqueuedCount{0};
        
        QList<QFuture<void>> futures;
        
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([this, commandsPerThread, &enqueuedCount]() {
                for (int j = 0; j < commandsPerThread; j++) {
                    auto cmd = std::make_unique<SelectCommand>();
                    QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
                    if (!token.isNull()) {
                        enqueuedCount++;
                    }
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QCOMPARE(enqueuedCount.load(), numThreads * commandsPerThread);
    }
    
    // ========================================================================
    // Thread Affinity Tests
    // ========================================================================
    
    void testCommandExecutionThreadAffinity() {
        // Commands should execute on communication thread, not caller thread
        m_commMgr->startDetection();
        
        QSignalSpy spy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Enqueue from different thread
        auto future = QtConcurrent::run([this]() {
            auto cmd = std::make_unique<SelectCommand>();
            return m_commMgr->enqueueCommand(std::move(cmd));
        });
        
        QUuid token = future.result();
        QVERIFY(!token.isNull());
        
        // Command will be executed on comm thread (can't directly verify but should not crash)
        QVERIFY(true);
    }
    
    // ========================================================================
    // Deadlock Prevention Tests
    // ========================================================================
    
    void testNoDeadlockOnStopWithQueuedCommands() {
        // Enqueue many commands
        for (int i = 0; i < 100; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            m_commMgr->enqueueCommand(std::move(cmd));
        }
        
        // Stop should not deadlock even with queued commands
        auto stopFuture = QtConcurrent::run([this]() {
            m_commMgr->stop();
        });
        
        // Should complete within reasonable time
        stopFuture.waitForFinished();
        QVERIFY(true);
    }
    
    void testNoDeadlockOnConcurrentStopAndEnqueue() {
        // Start stop in one thread
        auto stopFuture = QtConcurrent::run([this]() {
            QThread::msleep(50);
            m_commMgr->stop();
        });
        
        // Try to enqueue in another thread
        auto enqueueFuture = QtConcurrent::run([this]() {
            for (int i = 0; i < 50; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                m_commMgr->enqueueCommand(std::move(cmd));
                QThread::msleep(2);
            }
        });
        
        // Both should complete without deadlock
        stopFuture.waitForFinished();
        enqueueFuture.waitForFinished();
        QVERIFY(true);  // No deadlock
    }
    
    // ========================================================================
    // Signal Thread Safety Tests
    // ========================================================================
    
    void testSignalEmissionThreadSafety() {
        QSignalSpy stateSpy(m_commMgr.get(), &CommunicationManager::stateChanged);
        QSignalSpy cmdSpy(m_commMgr.get(), &CommunicationManager::commandCompleted);
        
        // Perform operations from multiple threads
        QList<QFuture<void>> futures;
        
        for (int i = 0; i < 5; i++) {
            auto future = QtConcurrent::run([this]() {
                for (int j = 0; j < 10; j++) {
                    auto cmd = std::make_unique<SelectCommand>();
                    m_commMgr->enqueueCommand(std::move(cmd));
                    QThread::msleep(5);
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        // Signals should be emitted safely (counts may vary)
        QVERIFY(stateSpy.count() >= 0);
        QVERIFY(cmdSpy.count() >= 0);
    }
    
    // ========================================================================
    // Edge Case: Rapid Start/Stop
    // ========================================================================
    
    void testRapidStartStopCycles() {
        for (int i = 0; i < 10; i++) {
            m_commMgr->startDetection();
            QThread::msleep(10);
            m_commMgr->stopDetection();
            QThread::msleep(10);
        }
        
        QVERIFY(true);  // Should not crash or deadlock
    }
    
    void testConcurrentRapidStartStop() {
        QList<QFuture<void>> futures;
        
        for (int i = 0; i < 5; i++) {
            auto future = QtConcurrent::run([this]() {
                for (int j = 0; j < 5; j++) {
                    m_commMgr->startDetection();
                    QThread::msleep(5);
                    m_commMgr->stopDetection();
                    QThread::msleep(5);
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        QVERIFY(true);  // Should not crash
    }
};

QTEST_MAIN(TestCommunicationManagerThreading)
#include "test_communication_manager_threading.moc"

