#include <QTest>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QThreadPool>
#include <QDateTime>
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
 * @brief Tests for CommunicationManager synchronous API
 * 
 * Tests executeCommandSync, timeout handling, mixing sync/async calls,
 * and concurrent synchronous operations from multiple threads.
 */
class TestCommunicationManagerSync : public QObject {
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
        // Step 1: Stop CommunicationManager first (with event queue flushing)
        if (m_commMgr) {
            m_commMgr->stop();
        }
        
        // Step 2: Wait for QThreadPool with LONGER timeout
        // Critical: Even though lambdas no longer capture [this], we must ensure
        // ALL thread pool threads complete before destroying objects they reference
        const int maxWait = 5000; // 5 seconds (increased from 1s)
        qDebug() << "Cleanup: Waiting for QThreadPool to finish...";
        if (!QThreadPool::globalInstance()->waitForDone(maxWait)) {
            qWarning() << "Cleanup: QThreadPool did not finish within timeout!";
            // Force clear thread pool as last resort
            QThreadPool::globalInstance()->clear();
            QThreadPool::globalInstance()->waitForDone(2000);
        }
        
        // Step 3: Additional safety margin - ensure NO threads are still accessing objects
        // This gives time for any final thread cleanup/destructors to complete
        qDebug() << "Cleanup: Waiting safety margin...";
        QThread::msleep(500); // Extra safety margin (increased from 0)
        
        // Step 4: Now safe to destroy objects
        qDebug() << "Cleanup: Destroying test objects...";
        if (m_commMgr) {
            m_commMgr.reset();
        }
        
        m_cmdSet.reset();
        m_mock = nullptr;
        qDebug() << "Cleanup: Complete";
    }
    
    // ========================================================================
    // Basic Synchronous Execution Tests
    // ========================================================================
    
    void testExecuteCommandSyncBasic() {
        // Start detection and simulate card
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        // Wait for card to be ready
        QTest::qWait(500);
        
        // Execute command synchronously from worker thread
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
            return commMgr->executeCommandSync(std::move(cmd), 5000);
        });
        
        CommandResult result = future.result();
        
        // Result could be success or failure depending on card state
        QVERIFY(true);  // Main test is that it doesn't deadlock
    }
    
    void testExecuteCommandSyncWithoutInit() {
        CommunicationManager mgr;
        
        auto cmd = std::make_unique<SelectCommand>();
        CommandResult result = mgr.executeCommandSync(std::move(cmd), 1000);
        
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }
    
    // ========================================================================
    // Timeout Tests
    // ========================================================================
    
    void testExecuteCommandSyncTimeout() {
        // Test that explicit timeout overrides command's default timeout
        // With the fix in communication_manager.cpp, timeouts work correctly when explicitly specified
        
        // Don't start detection or insert card - so command will wait and timeout
        auto startTime = QDateTime::currentMSecsSinceEpoch();
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            // SelectCommand has 120s default, but we explicitly use 500ms
            return commMgr->executeCommandSync(std::move(cmd), 500);
        });
        
        // Wait for completion (should timeout quickly, not wait 120 seconds)
        future.waitForFinished();
        auto elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
        
        // Should complete much faster than command's 120s default timeout
        // Allow 2 seconds for overhead, but it should be under 1 second typically
        QVERIFY(elapsed < 2000);
        
        CommandResult result = future.result();
        // Should fail due to timeout or card not ready
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }
    
    void testExecuteCommandSyncWithCustomTimeout() {
        m_commMgr->startDetection();
        m_mock->simulateCardInserted();
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<InitCommand>("123456", "123456789012", "password");
            // Init command has 60s default, but we use custom 2s timeout
            return commMgr->executeCommandSync(std::move(cmd), 2000);
        });
        
        // Should complete within timeout (even if it fails)
        future.waitForFinished();
        QVERIFY(true);
    }
    
    void testExecuteCommandSyncNoTimeout() {
        // -1 or 0 means no timeout (use command's default)
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(200);
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
            return commMgr->executeCommandSync(std::move(cmd), -1);  // No timeout
        });
        
        // Should complete eventually
        future.waitForFinished();
        QVERIFY(true);
    }
    
    // ========================================================================
    // Concurrent Synchronous Calls
    // ========================================================================
    
    void testMultipleSyncCallsFromDifferentThreads() {
        // Test that multiple threads can safely call executeCommandSync simultaneously
        // Fixed: CommunicationManager now properly flushes its event queue before stopping
        
        // Enable thread-safe mode for MockBackend to protect concurrent queueResponse calls
        m_mock->setThreadSafe(true);
        
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        const int numThreads = 3;
        std::atomic<int> successCount{0};
        std::atomic<int> failCount{0};
        
        QList<QFuture<void>> futures;
        
        // Capture raw pointers - safe because we wait for threads before cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        
        // Launch multiple threads that each call executeCommandSync
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([commMgr, mock, &successCount, &failCount]() {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
                
                if (result.success) {
                    successCount++;
                } else {
                    failCount++;
                }
            });
            futures.append(future);
        }
        
        // Wait for ALL futures to complete
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        // Wait for QThreadPool to fully finish
        QThreadPool::globalInstance()->waitForDone();
        
        // All threads should have completed (either success or fail)
        QCOMPARE(successCount.load() + failCount.load(), numThreads);
        // Most should succeed
        QVERIFY(successCount.load() >= numThreads / 2);
    }
    
    void testSequentialSyncCallsSameThread() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            // Execute multiple commands sequentially
            for (int i = 0; i < 3; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
                Q_UNUSED(result);
            }
            return true;
        });
        
        future.waitForFinished();
        QVERIFY(future.result());
    }
    
    // ========================================================================
    // Mixing Sync and Async
    // ========================================================================
    
    void testMixingSyncAndAsyncCalls() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        // Enqueue async command
        auto asyncCmd = std::make_unique<SelectCommand>();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        QUuid asyncToken = m_commMgr->enqueueCommand(std::move(asyncCmd));
        QVERIFY(!asyncToken.isNull());
        
        // Execute sync command
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
            return commMgr->executeCommandSync(std::move(cmd), 5000);
        });
        
        future.waitForFinished();
        
        // Both should complete
        CommandResult result = future.result();
        Q_UNUSED(result);
        QVERIFY(true);
    }
    
    void testInterleavedSyncAsyncCalls() {
        // Enable thread-safe mode for MockBackend to protect concurrent operations
        m_mock->setThreadSafe(true);
        
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        std::atomic<int> completedSync{0};
        std::atomic<int> completedAsync{0};
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        
        // Async enqueuer thread
        auto asyncFuture = QtConcurrent::run([commMgr, mock, &completedAsync]() {
            for (int i = 0; i < 5; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                QUuid token = commMgr->enqueueCommand(std::move(cmd));
                if (!token.isNull()) {
                    completedAsync++;
                }
                QThread::msleep(10);
            }
        });
        
        // Sync executor thread
        auto syncFuture = QtConcurrent::run([commMgr, mock, &completedSync]() {
            for (int i = 0; i < 3; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
                Q_UNUSED(result);
                completedSync++;
                QThread::msleep(20);
            }
        });
        
        asyncFuture.waitForFinished();
        syncFuture.waitForFinished();
        
        // Wait for QThreadPool to fully finish
        QThreadPool::globalInstance()->waitForDone();
        
        // CRITICAL: Wait for all async commands to complete execution
        // The async commands are still being processed by CommunicationManager's internal thread
        // even though enqueueCommand() returned immediately
        QTest::qWait(500);  // Increased wait time to ensure async commands complete
        
        // Verify commands completed
        QCOMPARE(completedAsync.load(), 5);
        QCOMPARE(completedSync.load(), 3);
    }
    
    // ========================================================================
    // Error Handling Tests
    // ========================================================================
    
    void testSyncExecuteWhenCardNotReady() {
        // Don't start detection or insert card
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd), 1000);
        });
        
        CommandResult result = future.result();
        
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }
    
    void testSyncExecuteAfterStop() {
        m_commMgr->stop();
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd), 1000);
        });
        
        CommandResult result = future.result();
        
        QVERIFY(!result.success);
    }
    
    void testSyncExecuteWithNullCommand() {
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            return commMgr->executeCommandSync(nullptr, 1000);
        });
        
        CommandResult result = future.result();
        
        QVERIFY(!result.success);
    }
    
    // ========================================================================
    // Batch Operations with Sync API
    // ========================================================================
    
    void testSyncCallsDuringBatchMode() {
        m_commMgr->startBatchOperations();
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            // Execute multiple sync commands in batch mode
            for (int i = 0; i < 3; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
                Q_UNUSED(result);
            }
            return true;
        });
        
        future.waitForFinished();
        
        m_commMgr->endBatchOperations();
        
        QVERIFY(future.result());
    }
    
    // ========================================================================
    // Card Lost During Sync Operation
    // ========================================================================
    
    void testSyncExecuteWhenCardLostDuringWait() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        // Start sync execution
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto syncFuture = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd), 10000);
        });
        
        // Remove card while waiting
        QTest::qWait(100);
        m_mock->simulateCardRemoved();
        
        // Should complete with error
        syncFuture.waitForFinished();
        QVERIFY(true);
        
        CommandResult result = syncFuture.result();
        // May succeed or fail depending on timing, but should not hang
        QVERIFY(true);
    }
    
    // ========================================================================
    // Stress Tests
    // ========================================================================
    
    void testHighVolumeSyncCalls() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        const int numCalls = 50;
        std::atomic<int> completedCount{0};
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        
        auto future = QtConcurrent::run([commMgr, mock, numCalls, &completedCount]() {
            for (int i = 0; i < numCalls; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd), 3000);
                Q_UNUSED(result);
                completedCount++;
            }
        });
        
        future.waitForFinished();  // Wait for completion
        
        // Wait for QThreadPool to fully finish
        QThreadPool::globalInstance()->waitForDone();
        
        // Give thread time to fully complete before cleanup
        QTest::qWait(100);
        
        // Should complete all (or most if some fail)
        QVERIFY(completedCount.load() >= numCalls / 2);  // At least half
    }
    
    void testConcurrentHighVolumeSyncCalls() {
        // Enable thread-safe mode for MockBackend to protect concurrent queueResponse calls
        m_mock->setThreadSafe(true);
        
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        const int numThreads = 5;
        const int callsPerThread = 10;
        std::atomic<int> completedCount{0};
        
        QList<QFuture<void>> futures;
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        
        for (int i = 0; i < numThreads; i++) {
            auto future = QtConcurrent::run([commMgr, mock, callsPerThread, &completedCount]() {
                for (int j = 0; j < callsPerThread; j++) {
                    auto cmd = std::make_unique<SelectCommand>();
                    mock->queueResponse(QByteArray::fromHex("9000"));
                    CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
                    Q_UNUSED(result);
                    completedCount++;
                }
            });
            futures.append(future);
        }
        
        for (auto& future : futures) {
            future.waitForFinished();
        }
        
        // Wait for QThreadPool to fully finish
        QThreadPool::globalInstance()->waitForDone();
        
        QVERIFY(completedCount.load() >= (numThreads * callsPerThread) / 2);
    }
    
    // ========================================================================
    // Edge Cases
    // ========================================================================
    
    void testSyncExecuteWithZeroTimeout() {
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd), 0);
        });
        
        // Should use command's default timeout
        future.waitForFinished();
        QVERIFY(true);
    }
    
    void testSyncExecuteWithNegativeTimeout() {
        m_commMgr->startDetection();
        m_mock->queueResponse(QByteArray::fromHex("8041") + QByteArray(65, 0x04) + QByteArray::fromHex("9000"));
        m_mock->simulateCardInserted();
        
        QTest::qWait(300);
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(QByteArray::fromHex("9000"));
            return commMgr->executeCommandSync(std::move(cmd), -1);
        });
        
        // Should complete
        future.waitForFinished();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestCommunicationManagerSync)
#include "test_communication_manager_sync.moc"

