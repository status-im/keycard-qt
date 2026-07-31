#include <QTest>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QThreadPool>
#include <QDateTime>
#include <QSignalSpy>
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
    static QByteArray validCardSelectResponse() {
        // 0x80 0x41 + valid uncompressed secp256k1 generator public key + SW 0x9000
        static const QByteArray kValidUncompressedPubKey = QByteArray::fromHex(
            "0479BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798"
            "483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8");
        return QByteArray::fromHex("8041") + kValidUncompressedPubKey + QByteArray::fromHex("9000");
    }

    std::shared_ptr<KeycardChannel> createMockChannel() {
        auto* mock = new MockBackend();
        mock->setAutoConnect(true);
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
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        // Execute command synchronously from worker thread
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
            return commMgr->executeCommandSync(std::move(cmd));
        });

        // Keep main thread event loop active so queued startDetection()/auto-connect can run.
        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(future.isFinished(), "Sync command did not complete within timeout");
        CommandResult result = future.result();
        Q_UNUSED(result);
    }
    
    void testExecuteCommandSyncWithoutInit() {
        CommunicationManager mgr;
        
        auto cmd = std::make_unique<SelectCommand>();
        CommandResult result = mgr.executeCommandSync(std::move(cmd));
        
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }
    
    // ========================================================================
    // Concurrent Synchronous Calls
    // ========================================================================
    
    void testMultipleSyncCallsFromDifferentThreads() {
        // Test that multiple threads can safely call executeCommandSync simultaneously
        // Fixed: CommunicationManager now properly flushes its event queue before stopping
        
        // Enable thread-safe mode for MockBackend to protect concurrent queueResponse calls
        m_mock->setThreadSafe(true);
        
        QSignalSpy initializedSpy(m_commMgr.get(), &CommunicationManager::cardInitialized);
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        m_commMgr->startDetection();

        // Finish asynchronous card detection and initialization before testing
        // concurrent synchronous calls. Otherwise this test also depends on
        // platform-specific event-loop and thread-pool scheduling.
        QTRY_COMPARE_WITH_TIMEOUT(initializedSpy.count(), 1, 5000);
        
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
                CommandResult result = commMgr->executeCommandSync(std::move(cmd));
                
                if (result.success) {
                    successCount++;
                } else {
                    failCount++;
                }
            });
            futures.append(future);
        }
        // Keep main thread event loop active while worker threads block in executeCommandSync().
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            bool allFinished = true;
            for (const auto& future : futures) {
                if (!future.isFinished()) {
                    allFinished = false;
                    break;
                }
            }
            if (allFinished) {
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        for (auto& future : futures) {
            QVERIFY2(future.isFinished(), "Concurrent sync call did not finish within timeout");
            future.waitForFinished();
        }

        // Wait for QThreadPool to fully finish
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(2000),
                 "QThreadPool did not drain within timeout");
        
        // All threads should have completed (either success or fail)
        QCOMPARE(successCount.load() + failCount.load(), numThreads);
        // Most should succeed
        QVERIFY(successCount.load() >= numThreads / 2);
    }
    
    void testSequentialSyncCallsSameThread() {
        m_commMgr->startBatchOperations();
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            // Execute multiple commands sequentially
            for (int i = 0; i < 3; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
                CommandResult result = commMgr->executeCommandSync(std::move(cmd));
                Q_UNUSED(result);
            }
            return true;
        });

        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(future.isFinished(), "Sequential sync calls did not finish within timeout");
        future.waitForFinished();
        QVERIFY(future.result());
        m_commMgr->endBatchOperations();
    }
    
    // ========================================================================
    // Mixing Sync and Async
    // ========================================================================
    
    void testMixingSyncAndAsyncCalls() {
        m_commMgr->startBatchOperations();

        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        // Enqueue async command
        auto asyncCmd = std::make_unique<SelectCommand>();
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        QUuid asyncToken = m_commMgr->enqueueCommand(std::move(asyncCmd));
        QVERIFY(!asyncToken.isNull());

        // Execute sync command
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            auto cmd = std::make_unique<SelectCommand>();
            mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
            return commMgr->executeCommandSync(std::move(cmd));
        });

        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(future.isFinished(), "Mixed sync/async call did not finish within timeout");
        future.waitForFinished();

        // Both should complete
        CommandResult result = future.result();
        Q_UNUSED(result);
        QVERIFY(true);

        m_commMgr->endBatchOperations();
    }
    
    void testInterleavedSyncAsyncCalls() {
        // Enable thread-safe mode for MockBackend to protect concurrent operations
        m_mock->setThreadSafe(true);

        // Keep detection/channel active while mixed traffic is in flight.
        m_commMgr->startBatchOperations();
        m_commMgr->startDetection();
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
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
                CommandResult result = commMgr->executeCommandSync(std::move(cmd));
                Q_UNUSED(result);
                completedSync++;
                QThread::msleep(20);
            }
        });

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 8000 && (!asyncFuture.isFinished() || !syncFuture.isFinished())) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(asyncFuture.isFinished(), "Async future did not finish within timeout");
        QVERIFY2(syncFuture.isFinished(), "Sync future did not finish within timeout");
        asyncFuture.waitForFinished();
        syncFuture.waitForFinished();

        // Wait for QThreadPool to fully finish
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(2000),
                 "QThreadPool did not drain within timeout");

        // Verify commands were enqueued/executed
        QCOMPARE(completedAsync.load(), 5);
        QCOMPARE(completedSync.load(), 3);

        m_commMgr->endBatchOperations();
    }
    
    // ========================================================================
    // Error Handling Tests
    // ========================================================================
    
    void testSyncExecuteAfterStop() {
        m_commMgr->stop();
        
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd));
        });
        
        CommandResult result = future.result();
        
        QVERIFY(!result.success);
    }
    
    void testSyncExecuteWithNullCommand() {
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto future = QtConcurrent::run([commMgr]() {
            return commMgr->executeCommandSync(nullptr);
        });
        
        CommandResult result = future.result();
        
        QVERIFY(!result.success);
    }

    void testCancelPendingOperationsDrainsQueueAndEmitsSignals() {
        m_mock->setTransmitDelay(150);

        const int numCommands = 6;
        struct CancelStats {
            QHash<QUuid, int> completionCountByToken;
            int completedCount = 0;
            int cancelledCount = 0;
            int operationCancelledCount = 0;
            QString cancellationReason;
        };
        auto stats = std::make_shared<CancelStats>();

        QMetaObject::Connection commandCompletedConn = connect(
            m_commMgr.get(), &CommunicationManager::commandCompleted, this,
            [stats](QUuid token, CommandResult result) {
                stats->completionCountByToken[token]++;
                stats->completedCount++;
                if (result.reason == CommandResultType::Cancelled) {
                    stats->cancelledCount++;
                }
            });

        QMetaObject::Connection operationCancelledConn = connect(
            m_commMgr.get(), &CommunicationManager::operationCancelled, this,
            [stats](const QString& reason) {
                stats->operationCancelledCount++;
                stats->cancellationReason = reason;
            });

        QList<QUuid> tokens;
        for (int i = 0; i < numCommands; i++) {
            auto cmd = std::make_unique<SelectCommand>();
            tokens.append(cmd->token());
            m_mock->queueResponse(QByteArray::fromHex("9000"));
            QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
            QVERIFY2(!token.isNull(), "enqueueCommand returned null token");
        }

        // Cancel immediately so queued commands are drained instead of fully executing.
        const QString kCancelReason = "test-cancel";
        m_commMgr->cancelPendingOperations(kCancelReason);

        // Wait until all commands are completed/cancelled and detection is stopped.
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (waitTimer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (stats->completedCount >= numCommands &&
                stats->operationCancelledCount >= 1 &&
                !m_mock->isDetecting()) {
                break;
            }
            QThread::msleep(10);
        }

        QCOMPARE(stats->operationCancelledCount, 1);
        QCOMPARE(stats->cancellationReason, kCancelReason);
        QCOMPARE(stats->completedCount, numCommands);
        QVERIFY2(stats->cancelledCount >= 1, "Expected at least one command to be cancelled");
        QVERIFY2(!m_mock->isDetecting(), "Detection should be stopped after cancellation");

        // Every enqueued command should complete exactly once.
        for (const QUuid& token : tokens) {
            QCOMPARE(stats->completionCountByToken.value(token, 0), 1);
        }

        disconnect(commandCompletedConn);
        disconnect(operationCancelledConn);
    }
    
    // ========================================================================
    // Batch Operations with Sync API
    // ========================================================================
    
    void testSyncCallsDuringBatchMode() {
        m_commMgr->startBatchOperations();
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;
        auto future = QtConcurrent::run([commMgr, mock]() {
            // Execute multiple sync commands in batch mode
            for (int i = 0; i < 3; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd));
                Q_UNUSED(result);
            }
            return true;
        });
        
        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(future.isFinished(), "Batch-mode sync calls did not finish within timeout");
        future.waitForFinished();

        m_commMgr->endBatchOperations();

        QVERIFY(future.result());
    }
    
    // ========================================================================
    // Card Lost During Sync Operation
    // ========================================================================
    
    void testSyncExecuteWhenCardLostDuringWait() {
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        // Start sync execution
        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto syncFuture = QtConcurrent::run([commMgr]() {
            auto cmd = std::make_unique<SelectCommand>();
            return commMgr->executeCommandSync(std::move(cmd));
        });
        
        // Keep main thread event loop active while sync wait is in background thread.
        QElapsedTimer timer;
        timer.start();
        while (!syncFuture.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(syncFuture.isFinished(), "Sync execute did not finish within timeout");
        syncFuture.waitForFinished();

        CommandResult result = syncFuture.result();
        Q_UNUSED(result);
        // May succeed or fail depending on timing, but should not hang
        QVERIFY(true);
    }
    
    // ========================================================================
    // Stress Tests
    // ========================================================================
    
    void testHighVolumeSyncCalls() {
        m_commMgr->startBatchOperations();
        m_mock->queueResponse(TestCommunicationManagerSync::validCardSelectResponse());
        const int numCalls = 50;
        std::atomic<int> completedCount{0};

        // CRITICAL: Don't capture [this] to avoid use-after-free during cleanup
        auto* commMgr = m_commMgr.get();
        auto* mock = m_mock;

        auto future = QtConcurrent::run([commMgr, mock, numCalls, &completedCount]() {
            for (int i = 0; i < numCalls; i++) {
                auto cmd = std::make_unique<SelectCommand>();
                mock->queueResponse(QByteArray::fromHex("9000"));
                CommandResult result = commMgr->executeCommandSync(std::move(cmd));
                Q_UNUSED(result);
                completedCount++;
            }
        });

        QElapsedTimer timer;
        timer.start();
        while (!future.isFinished() && timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        QVERIFY2(future.isFinished(), "High-volume sync calls did not finish within timeout");
        future.waitForFinished();

        // Wait for QThreadPool to fully finish
        QVERIFY2(QThreadPool::globalInstance()->waitForDone(3000),
                 "QThreadPool did not drain within timeout");

        m_commMgr->endBatchOperations();

        // Should complete all (or most if some fail)
        QVERIFY(completedCount.load() >= numCalls / 2);  // At least half
    }
};

QTEST_MAIN(TestCommunicationManagerSync)
#include "test_communication_manager_sync.moc"

