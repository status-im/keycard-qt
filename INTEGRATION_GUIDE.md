# Integration Guide: Queue-Based Architecture

## Overview

This guide shows how to integrate the new CommunicationManager-based architecture with existing SessionManager and CommandSet code.

## Integration Strategy

We'll use a **gradual migration** approach:

1. **Phase 3.1**: Add CommunicationManager as optional dependency
2. **Phase 3.2**: Modify SessionManager to use CommunicationManager
3. **Phase 3.3**: Keep CommandSet backward compatible for direct use
4. **Phase 3.4**: Add feature flag to switch between architectures

## Current Architecture (Before)

```
┌─────────────────┐
│  SessionManager │
│  (main thread)  │
└────────┬────────┘
         │ QtConcurrent::run()
         ↓
    ┌────────────────┐         ┌──────────────┐
    │ Background     │────────→│  CommandSet  │
    │ Thread         │         │              │
    └────────────────┘         └──────┬───────┘
                                      │
    ┌────────────────┐                │
    │ Request Thread │────────────────┘
    └────────────────┘         ↓
                         ┌──────────────┐
                         │KeycardChannel│
                         └──────────────┘
                         
PROBLEM: Both threads access card simultaneously → RACE!
```

## New Architecture (After)

```
┌─────────────────┐
│  SessionManager │
│  (main thread)  │
└────────┬────────┘
         │
         ↓
    ┌────────────────────────┐
    │ CommunicationManager   │
    │   (Queue Dispatcher)   │
    └────────────┬───────────┘
                 │
    ┌────────────↓───────────┐
    │  Communication Thread  │
    │  ┌──────────────────┐  │
    │  │  Command Queue   │  │
    │  └────────┬─────────┘  │
    │           ↓             │
    │  ┌──────────────────┐  │
    │  │   CommandSet     │  │
    │  └────────┬─────────┘  │
    │           ↓             │
    │  ┌──────────────────┐  │
    │  │ KeycardChannel   │  │
    │  └──────────────────┘  │
    └────────────────────────┘
    
SOLUTION: Single thread owns card → NO RACES!
```

## Step-by-Step Integration

### Step 1: Update SessionManager Constructor

Add CommunicationManager as a member:

```cpp
// session_manager.h
class SessionManager : public QObject {
    Q_OBJECT
    
public:
    explicit SessionManager(QObject* parent = nullptr);
    
    // New: Set whether to use new architecture
    void setUseNewArchitecture(bool enable) { m_useNewArchitecture = enable; }
    
private:
    // Existing members
    std::shared_ptr<Keycard::CommandSet> m_commandSet;
    std::shared_ptr<Keycard::KeycardChannel> m_channel;
    QMutex m_operationMutex;
    
    // NEW: Communication manager (optional)
    std::unique_ptr<Keycard::CommunicationManager> m_commMgr;
    bool m_useNewArchitecture = false;
};
```

### Step 2: Initialize CommunicationManager in start()

```cpp
// session_manager.cpp
bool SessionManager::start(bool logEnabled, const QString& logFilePath) {
    // ... existing initialization ...
    
    if (m_useNewArchitecture) {
        qDebug() << "SessionManager: Using NEW queue-based architecture";
        
        // Create CommunicationManager
        m_commMgr = std::make_unique<Keycard::CommunicationManager>(this);
        
        // Connect to signals
        connect(m_commMgr.get(), &Keycard::CommunicationManager::cardInitialized,
                this, &SessionManager::onCardInitializedNew);
        
        connect(m_commMgr.get(), &Keycard::CommunicationManager::cardLost,
                this, &SessionManager::onCardRemovedNew);
        
        // Start it
        if (!m_commMgr->start(m_channel, m_pairingStorage, m_passwordProvider)) {
            qWarning() << "SessionManager: Failed to start CommunicationManager";
            return false;
        }
        
        setState(SessionState::WaitingForCard);
        
    } else {
        qDebug() << "SessionManager: Using OLD direct architecture";
        
        // Old path - keep existing code
        if (m_channel) {
            m_channel->setState(Keycard::ChannelState::WaitingForCard);
            setState(SessionState::WaitingForCard);
        } else {
            setState(SessionState::WaitingForReader);
        }
        
        // Old signal connections
        connect(m_channel.get(), &Keycard::KeycardChannel::targetDetected,
                this, &SessionManager::onCardDetected);
        connect(m_channel.get(), &Keycard::KeycardChannel::targetLost,
                this, &SessionManager::onCardRemoved);
    }
    
    m_started = true;
    return true;
}
```

### Step 3: Add New Card Detection Handler

```cpp
// session_manager.cpp
void SessionManager::onCardInitializedNew(Keycard::CardInitializationResult result) {
    qDebug() << "SessionManager::onCardInitializedNew() success:" << result.success;
    
    if (result.success) {
        m_appInfo = result.appInfo;
        m_appStatus = result.appStatus;
        
        if (!result.appInfo.initialized) {
            setState(SessionState::EmptyKeycard);
        } else {
            setState(SessionState::Ready);
        }
    } else {
        qWarning() << "SessionManager: Card initialization failed:" << result.error;
        setError(result.error);
        setState(SessionState::ConnectionError);
    }
}

void SessionManager::onCardRemovedNew() {
    qDebug() << "SessionManager::onCardRemovedNew()";
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    qDebug() << "Ignoring card removal on mobile";
    return;
#else
    m_currentCardUID.clear();
    
    if (m_started) {
        setState(SessionState::WaitingForCard);
    }
#endif
}
```

### Step 4: Update Card Operations

Modify operations to use CommunicationManager when enabled:

```cpp
bool SessionManager::authorize(const QString& pin) {
    qDebug() << "SessionManager::authorize()";
    
    if (m_useNewArchitecture) {
        // NEW: Use CommunicationManager
        auto cmd = std::make_unique<Keycard::VerifyPINCommand>(pin);
        Keycard::CommandResult result = m_commMgr->executeCommandSync(std::move(cmd), 30000);
        
        if (result.success) {
            // Update status from result
            QVariantMap data = result.data.toMap();
            m_appStatus.pinRetryCount = data["remainingAttempts"].toInt();
            
            setState(SessionState::Authorized);
            return true;
        } else {
            setError(result.error);
            return false;
        }
        
    } else {
        // OLD: Direct CommandSet call
        QMutexLocker locker(&m_operationMutex);
        
        if (m_state != SessionState::Ready) {
            setError("Card not ready");
            return false;
        }
        
        bool result = m_commandSet->verifyPIN(pin);
        m_appStatus = m_commandSet->cachedApplicationStatus();
        
        if (result) {
            setState(SessionState::Authorized);
        } else {
            setError(m_commandSet->lastError());
        }
        
        return result;
    }
}
```

### Step 5: Other Operations Follow Same Pattern

```cpp
bool SessionManager::initialize(const QString& pin, const QString& puk, 
                                 const QString& pairingPassword) {
    if (m_useNewArchitecture) {
        // NEW
        auto cmd = std::make_unique<Keycard::InitializeCommand>(pin, puk, pairingPassword);
        Keycard::CommandResult result = m_commMgr->executeCommandSync(std::move(cmd), 60000);
        
        if (result.success) {
            // Card is now initialized, need to re-detect
            setState(SessionState::WaitingForCard);
            return true;
        } else {
            setError(result.error);
            return false;
        }
    } else {
        // OLD - existing code
        QMutexLocker locker(&m_operationMutex);
        // ... existing implementation ...
    }
}

QVector<int> SessionManager::generateMnemonic(int length) {
    if (m_useNewArchitecture) {
        // NEW
        int checksumSize = length / 3;  // Convert length to checksum size
        auto cmd = std::make_unique<Keycard::GenerateMnemonicCommand>(checksumSize);
        Keycard::CommandResult result = m_commMgr->executeCommandSync(std::move(cmd), 60000);
        
        if (result.success) {
            QVariantList list = result.data.toList();
            QVector<int> indexes;
            for (const QVariant& v : list) {
                indexes.append(v.toInt());
            }
            return indexes;
        } else {
            setError(result.error);
            return QVector<int>();
        }
    } else {
        // OLD - existing code
        QMutexLocker locker(&m_operationMutex);
        // ... existing implementation ...
    }
}
```

## Testing the Integration

### Unit Test Example

```cpp
#include <QTest>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/card_command.h"

class TestCommunicationManager : public QObject {
    Q_OBJECT
    
private slots:
    void testCommandQueue() {
        // Create mock channel
        auto channel = std::make_shared<MockKeycardChannel>();
        
        // Create manager
        CommunicationManager manager;
        manager.start(channel, nullptr, nullptr);
        
        // Enqueue commands
        auto cmd1 = std::make_unique<SelectCommand>();
        QUuid token1 = manager.enqueueCommand(std::move(cmd1));
        
        auto cmd2 = std::make_unique<GetStatusCommand>();
        QUuid token2 = manager.enqueueCommand(std::move(cmd2));
        
        // Wait for completion
        QSignalSpy spy(&manager, &CommunicationManager::commandCompleted);
        QVERIFY(spy.wait(5000));
        
        // Verify results
        QCOMPARE(spy.count(), 2);
        // Check that cmd1 completed before cmd2 (serial execution)
    }
    
    void testNoRaceCondition() {
        // Simulate card detection while commands are pending
        // Verify initialization completes before commands execute
    }
};

QTEST_MAIN(TestCommunicationManager)
#include "test_communication_manager.moc"
```

### Integration Test Example

```cpp
void testSessionManagerWithNewArchitecture() {
    SessionManager manager;
    manager.setUseNewArchitecture(true);  // Enable new architecture
    
    manager.start();
    
    // Wait for card detection
    QSignalSpy spy(&manager, &SessionManager::stateChanged);
    QVERIFY(spy.wait(30000));
    
    // Verify card initialized without race
    QCOMPARE(manager.currentState(), SessionState::Ready);
    
    // Perform operation
    bool success = manager.authorize("123456");
    QVERIFY(success);
    QCOMPARE(manager.currentState(), SessionState::Authorized);
}
```

## Feature Flag Configuration

Environment variable approach:

```cpp
bool SessionManager::shouldUseNewArchitecture() {
    // Check environment variable
    QByteArray env = qgetenv("KEYCARD_USE_NEW_ARCHITECTURE");
    if (!env.isEmpty()) {
        return env == "1" || env.toLower() == "true";
    }
    
    // Or check config file
    QSettings settings;
    return settings.value("keycard/useNewArchitecture", false).toBool();
}
```

## Rollout Plan

### Week 1: Internal Testing
- [ ] Enable for development builds only
- [ ] Test all flows manually
- [ ] Run automated test suite
- [ ] Fix any discovered issues

### Week 2: Canary Release
- [ ] Enable for 10% of users (random selection)
- [ ] Monitor error rates and metrics
- [ ] Collect feedback
- [ ] Adjust as needed

### Week 3: Gradual Rollout
- [ ] Increase to 25% of users
- [ ] Then 50%
- [ ] Then 75%
- [ ] Monitor continuously

### Week 4: Full Rollout
- [ ] Enable for 100% of users
- [ ] Mark old architecture as deprecated
- [ ] Plan removal of old code

## Metrics to Monitor

1. **Error Rates**: Track APDU failures, state machine errors
2. **Performance**: Measure operation latency, queue depth
3. **Stability**: Monitor crashes, hangs, timeouts
4. **User Experience**: Track operation success rates

## Rollback Plan

If issues are discovered:

1. **Immediate**: Set feature flag to false
2. **Deploy**: Push update with flag change
3. **Investigate**: Debug issues offline
4. **Fix**: Implement fixes
5. **Re-test**: Thorough testing before re-enabling

## Next Steps

1. ✅ **Phase 2 Complete**: Core infrastructure implemented
2. **Phase 3 Next**: Implement integration as described here
3. **Phase 4**: Add comprehensive tests
4. **Phase 5**: Gradual rollout with monitoring

## Benefits Summary

✅ **Solves Race Condition**: Card initialization is atomic
✅ **Thread Safe**: All card I/O on single thread  
✅ **Backward Compatible**: Old code path still works
✅ **Testable**: Easy to add tests for new architecture
✅ **Gradual Migration**: Can switch with feature flag
✅ **Clear Ownership**: Communication thread owns card

## Questions?

See:
- Architecture doc: `ARCHITECTURE_CHANGES.md`
- Original plan: `/Users/alexjbanca/.cursor/plans/keycard_architecture_analysis_92fa910b.plan.md`
- Go reference: `vendor/status-keycard-go/internal/keycard_context_v2.go`
