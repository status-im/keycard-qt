#include "keycard-qt/communication_manager.h"
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

namespace Keycard {

// ============================================================================
// CommunicationThread Implementation
// ============================================================================

void CommunicationManager::CommunicationThread::run() {
    qDebug() << "CommunicationThread: Starting on thread:" << QThread::currentThread();
    
    // CRITICAL: Lock thread for PC/SC operations (like Go's runtime.LockOSThread())
    // PC/SC requires operations to happen on the same thread that established context
#if defined(Q_OS_WIN) || defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // Platform-specific thread locking happens in PC/SC backend
    qDebug() << "CommunicationThread: Running on platform requiring thread affinity";
#endif
    
    // Run Qt event loop
    // All card operations will be processed through queued signals/slots
    exec();
    
    qDebug() << "CommunicationThread: Stopped";
}

// ============================================================================
// CommunicationManager Implementation
// ============================================================================

CommunicationManager::CommunicationManager(QObject* parent)
    : QObject(parent)
    , m_commThread(nullptr)
    , m_state(State::Idle)
    , m_running(false)
{
    qDebug() << "CommunicationManager: Created";
}

CommunicationManager::~CommunicationManager() {
    stop();
}

bool CommunicationManager::init(std::shared_ptr<KeycardChannel> channel,
                                 std::shared_ptr<IPairingStorage> pairingStorage,
                                 PairingPasswordProvider passwordProvider) {
    if (m_running) {
        qWarning() << "CommunicationManager: Already initialized";
        return false;
    }
    
    if (!channel) {
        qWarning() << "CommunicationManager: No channel provided";
        return false;
    }
    
    qDebug() << "CommunicationManager: Initializing...";
    
    m_channel = channel;
    m_pairingStorage = pairingStorage;
    m_passwordProvider = passwordProvider;
    
    // Create CommandSet
    m_commandSet = std::make_shared<CommandSet>(m_channel, m_pairingStorage, m_passwordProvider);
    
    // Disconnect CommandSet from channel signals to prevent race with our handlers
    QObject::disconnect(m_channel.get(), nullptr, m_commandSet.get(), nullptr);
    qDebug() << "CommunicationManager: Disconnected CommandSet from channel signals";
    
    // Create and start communication thread
    m_commThread = new CommunicationThread(this);
    
    // Move manager to communication thread so all slots run there
    moveToThread(m_commThread);
    
    // Connect channel signals (will be delivered on communication thread)
    connect(m_channel.get(), &KeycardChannel::targetDetected,
            this, &CommunicationManager::onCardDetected,
            Qt::QueuedConnection);
    
    connect(m_channel.get(), &KeycardChannel::targetLost,
            this, &CommunicationManager::onCardRemoved,
            Qt::QueuedConnection);
    
    m_commThread->start();
    
    m_running = true;
    setState(State::Idle);
    
    qDebug() << "CommunicationManager: Initialized successfully (detection not started yet)";
    return true;
}

bool CommunicationManager::startDetection() {
    if (!m_running) {
        qWarning() << "CommunicationManager: Not initialized, call init() first";
        return false;
    }
    
    if (!m_channel) {
        qWarning() << "CommunicationManager: No channel available";
        return false;
    }
    
    qDebug() << "CommunicationManager: Starting card detection...";
    
    // Start channel detection
    QMetaObject::invokeMethod(m_channel.get(), &KeycardChannel::startDetection,
                               Qt::QueuedConnection);
    
    qDebug() << "CommunicationManager: Card detection started";
    return true;
}

void CommunicationManager::stopDetection() {
    if (!m_channel) {
        return;
    }
    
    qDebug() << "CommunicationManager: Stopping card detection...";
    
    QMetaObject::invokeMethod(m_channel.get(), &KeycardChannel::stopDetection,
                               Qt::QueuedConnection);
    
    // Set channel to Idle
    if (m_channel) {
        m_channel->setState(ChannelState::Idle);
    }
    
    qDebug() << "CommunicationManager: Card detection stopped";
}

void CommunicationManager::stop() {
    if (!m_running) {
        return;
    }
    
    qDebug() << "CommunicationManager: Stopping completely...";
    
    m_running = false;
    
    // Stop card detection
    stopDetection();
    
    // Clear queue
    {
        QMutexLocker locker(&m_queueMutex);
        // std::queue doesn't have clear(), use swap with empty queue
        std::queue<std::unique_ptr<CardCommand>>().swap(m_queue);
        m_queueNotEmpty.wakeAll();
        
        // Set channel to Idle when stopping
        if (m_channel) {
            m_channel->setState(ChannelState::Idle);
        }
    }
    
    // Stop thread
    if (m_commThread) {
        m_commThread->quit();
        m_commThread->wait(5000);
        delete m_commThread;
        m_commThread = nullptr;
    }
    
    // Clear sync operations
    {
        QMutexLocker locker(&m_syncMutex);
        for (auto it = m_pendingSync.begin(); it != m_pendingSync.end(); ++it) {
            it.value()->completed = true;
            it.value()->result = CommandResult::fromError("CommunicationManager stopped");
            it.value()->condition.wakeAll();
        }
        m_pendingSync.clear();
    }
    
    setState(State::Idle);
    
    qDebug() << "CommunicationManager: Stopped";
}

QUuid CommunicationManager::enqueueCommand(std::unique_ptr<CardCommand> cmd) {
    if (!m_running) {
        qWarning() << "CommunicationManager: Cannot enqueue command, not running";
        return QUuid();
    }
    
    QUuid token = cmd->token();
    QString cmdName = cmd->name();
    
    qDebug() << "CommunicationManager: Enqueueing command" << cmdName << "token:" << token;
    
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.push(std::move(cmd));
        m_queueNotEmpty.wakeAll();
    }
    
    // Set channel to WaitingForCard when commands are enqueued
    // This triggers card detection/presentation:
    // - iOS: Opens NFC drawer for user to present card
    // - Android/Desktop: Continues polling (already active)
    if (m_channel) {
        QThread* currentThread = QThread::currentThread();
        QThread* channelThread = m_channel->thread();
        QThread* mainThread = QCoreApplication::instance()->thread();
        
        if (currentThread == channelThread) {
            // Same thread - call directly
            qDebug() << "CommunicationManager: Setting channel state directly (same thread)";
            m_channel->setState(ChannelState::WaitingForCard);
        } else if (currentThread == mainThread) {
            // Main thread calling, channel on different thread
            // Use QueuedConnection - will be processed by processEvents() in wait loop
            qDebug() << "CommunicationManager: Queueing channel state (main thread → channel thread)";
            QMetaObject::invokeMethod(m_channel.get(), [this]() {
                m_channel->setState(ChannelState::WaitingForCard);
            }, Qt::QueuedConnection);
        } else {
            // Background thread calling
            // Use BlockingQueuedConnection to ensure setState completes before we block
            qDebug() << "CommunicationManager: Blocking channel state (background thread → channel thread)";
            QMetaObject::invokeMethod(m_channel.get(), [this]() {
                m_channel->setState(ChannelState::WaitingForCard);
            }, Qt::BlockingQueuedConnection);
        }
    }
    
    // Trigger queue processing on communication thread
    QMetaObject::invokeMethod(this, &CommunicationManager::processQueue,
                               Qt::QueuedConnection);
    
    return token;
}

CommandResult CommunicationManager::executeCommandSync(std::unique_ptr<CardCommand> cmd, int timeoutMs) {
    if (!m_running) {
        return CommandResult::fromError("CommunicationManager not running");
    }
    
    QUuid token = cmd->token();
    QString cmdName = cmd->name();
    
    if (timeoutMs < 0) {
        timeoutMs = cmd->timeoutMs();
    }
    
    qDebug() << "CommunicationManager: Executing command synchronously:" << cmdName
             << "token:" << token << "timeout:" << timeoutMs
             << "thread:" << QThread::currentThread();
    
    // Create sync tracker
    PendingSync sync;
    sync.completed = false;
    
    {
        QMutexLocker locker(&m_syncMutex);
        m_pendingSync[token] = &sync;
    }
    
    // Enqueue command (this will set channel state)
    enqueueCommand(std::move(cmd));
    
    // IMPORTANT: Thread-safe wait strategy
    // Check if we're on the main/GUI thread
    QThread* mainThread = QCoreApplication::instance()->thread();
    QThread* currentThread = QThread::currentThread();
    bool isMainThread = (currentThread == mainThread);
    
    if (isMainThread) {
        // MAIN THREAD: Use event processing to keep UI responsive
        qDebug() << "CommunicationManager: Waiting on main thread - processing events";
        
        QElapsedTimer timer;
        timer.start();
        
        QMutexLocker locker(&m_syncMutex);
        
        while (!sync.completed && timer.elapsed() < timeoutMs) {
            // Unlock mutex to allow event processing
            locker.unlock();
            
            // Process Qt events (keeps UI responsive!)
            // SAFE: We checked we're on the main thread
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            
            // Re-lock and check completion
            locker.relock();
            
            if (!sync.completed) {
                sync.condition.wait(&m_syncMutex, 100);
            }
        }
    } else {
        // BACKGROUND THREAD: Use simple blocking wait
        qDebug() << "CommunicationManager: Waiting on background thread - blocking wait";
        
        QMutexLocker locker(&m_syncMutex);
        
        if (!sync.completed) {
            bool success = sync.condition.wait(&m_syncMutex, timeoutMs);
            if (!success) {
                qWarning() << "CommunicationManager: Sync command timed out:" << cmdName;
                sync.result = CommandResult::fromError("Command timeout");
            }
        }
    }
    
    // Check final result
    if (!sync.completed) {
        qWarning() << "CommunicationManager: Sync command timed out:" << cmdName;
        sync.result = CommandResult::fromError("Command timeout");
    }
    
    m_pendingSync.remove(token);
    
    return sync.result;
}

CommunicationManager::State CommunicationManager::state() const {
    QMutexLocker locker(&m_stateMutex);
    return m_state;
}

ApplicationInfo CommunicationManager::applicationInfo() const {
    QMutexLocker locker(&m_infoMutex);
    return m_appInfo;
}

ApplicationStatus CommunicationManager::applicationStatus() const {
    QMutexLocker locker(&m_infoMutex);
    return m_appStatus;
}

QByteArray CommunicationManager::getDataFromCard(uint8_t type) {
    if (!m_commandSet) {
        return QByteArray();
    }
    return m_commandSet->getData(type);
}

bool CommunicationManager::storeDataToCard(uint8_t type, const QByteArray& data) {
    if (!m_commandSet) {
        return false;
    }
    return m_commandSet->storeData(type, data);
}

void CommunicationManager::setState(State newState) {
    State oldState;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_state == newState) {
            return;
        }
        oldState = m_state;
        m_state = newState;
    }
    
    qDebug() << "CommunicationManager: State changed:" << oldState << "->" << newState;
    emit stateChanged(newState);
}

// ============================================================================
// Card Detection and Initialization (Communication Thread)
// ============================================================================

void CommunicationManager::onCardDetected(const QString& uid) {
    qDebug() << "========================================";
    qDebug() << "CommunicationManager: CARD DETECTED! UID:" << uid;
    qDebug() << "   Thread:" << QThread::currentThread();
    qDebug() << "   Current state:" << m_state;
    qDebug() << "========================================";
    
    // Check if we're already processing this card
    if (m_currentCardUID == uid && m_state != State::Idle) {
        qDebug() << "CommunicationManager: Same card already being processed, ignoring";
        return;
    }
    
    // Check if we should process this card detection
    if (m_state != State::Idle) {
        qDebug() << "CommunicationManager: Card detected but not in Idle state, ignoring";
        return;
    }
    
    m_currentCardUID = uid;
    setState(State::Initializing);
    
    // =========================================================================
    // CRITICAL: Run initialization sequence atomically
    // This is the key to preventing the race condition!
    // NO other operations can run until this completes.
    // =========================================================================
    
    qDebug() << "CommunicationManager: Starting card initialization sequence...";
    CardInitializationResult result = initializeCardSequence();
    
    if (result.success) {
        qDebug() << "CommunicationManager: Card initialization SUCCESS";
        
        // Update cached info
        {
            QMutexLocker locker(&m_infoMutex);
            m_appInfo = result.appInfo;
            m_appStatus = result.appStatus;
        }
        
        setState(State::Ready);
        emit cardInitialized(result);
        
        // Now process any queued commands
        processQueue();
        
    } else {
        qWarning() << "CommunicationManager: Card initialization FAILED:" << result.error;
        
        setState(State::Idle);
        emit cardInitialized(result);
    }
}

void CommunicationManager::onCardRemoved() {
    qDebug() << "========================================";
    qDebug() << "CommunicationManager: CARD REMOVED";
    qDebug() << "========================================";
    
    m_currentCardUID.clear();
    
    // Clear cached info
    {
        QMutexLocker locker(&m_infoMutex);
        m_appInfo = ApplicationInfo();
        m_appStatus = ApplicationStatus();
    }
    
    setState(State::Idle);
    emit cardLost();
}

CardInitializationResult CommunicationManager::initializeCardSequence() {
    qDebug() << "CommunicationManager::initializeCardSequence() - STARTING";
    qDebug() << "   This runs atomically - NO races possible!";
    
    if (!m_commandSet) {
        return CardInitializationResult::fromError("No CommandSet available");
    }
    
    // STEP 1: SELECT applet
    qDebug() << "   [1/5] SELECT applet...";
    ApplicationInfo appInfo = m_commandSet->select(true);
    
    if (!appInfo.installed && appInfo.instanceUID.isEmpty() && appInfo.secureChannelPublicKey.isEmpty()) {
        return CardInitializationResult::fromError("Failed to select applet");
    }
    
    // Check if card is initialized
    if (!appInfo.initialized) {
        qDebug() << "   Card is empty (not initialized)";
        // Return success with empty state - card needs initialization
        return CardInitializationResult::fromSuccess(m_currentCardUID, appInfo, ApplicationStatus());
    }
    
    // STEP 2: Ensure pairing
    qDebug() << "   [2/5] Ensure pairing...";
    if (!m_commandSet->ensurePairing()) {
        QString error = m_commandSet->lastError();
        if (error.isEmpty()) {
            error = "Failed to ensure pairing";
        }
        return CardInitializationResult::fromError(error);
    }
    
    // STEP 3: Open secure channel
    qDebug() << "   [3/5] Open secure channel...";
    if (!m_commandSet->ensureSecureChannel()) {
        QString error = m_commandSet->lastError();
        if (error.isEmpty()) {
            error = "Failed to open secure channel";
        }
        return CardInitializationResult::fromError(error);
    }
    
    // STEP 4: Get status
    qDebug() << "   [4/5] Get application status...";
    ApplicationStatus appStatus = m_commandSet->cachedApplicationStatus();
    
    if (!m_commandSet->hasCachedStatus()) {
        qWarning() << "   Failed to get application status, but continuing...";
    }
    
    // STEP 5: Get metadata (optional - don't fail if this errors)
    qDebug() << "   [5/5] Get metadata (optional)...";
    // We could add metadata fetching here if needed
    
    qDebug() << "CommunicationManager::initializeCardSequence() - COMPLETED SUCCESSFULLY";
    
    return CardInitializationResult::fromSuccess(m_currentCardUID, appInfo, appStatus);
}

// ============================================================================
// Command Queue Processing (Communication Thread)
// ============================================================================

void CommunicationManager::processQueue() {
    // This method runs on the communication thread
    // It processes commands one at a time, serially
    
    QMutexLocker locker(&m_queueMutex);
    
    if (m_queue.empty()) {
        // Set channel to Idle when queue is empty
        if (m_channel) {
            m_channel->setState(ChannelState::Idle);
        }
        return;
    }
    
    // Check if we can process commands
    State currentState = state();
    
    if (currentState == State::Processing) {
        qDebug() << "CommunicationManager: Already processing a command, skipping";
        return;
    }
    
    // If card is not detected/initialized yet, wait for it
    // The channel is already in WaitingForCard state (set by enqueueCommand)
    // When card is detected, onCardDetected will initialize and then processQueue will be called again
    if (currentState == State::Idle) {
        qDebug() << "CommunicationManager: Card not detected yet, waiting...";
        qDebug() << "CommunicationManager: Channel should be in WaitingForCard state";
        // Don't process yet - wait for card detection
        return;
    }
    
    // Get next command
    auto cmd = std::move(m_queue.front());
    m_queue.pop();
    QUuid token = cmd->token();
    QString cmdName = cmd->name();
    
    // Check if command can run in current state
    if (currentState == State::Initializing && !cmd->canRunDuringInit()) {
        qDebug() << "CommunicationManager: Command" << cmdName << "cannot run during init, re-queuing";
        // Re-queue at front (note: this changes FIFO order but ensures command runs after init)
        // For proper FIFO with re-queuing, we'd need a deque, but this is simpler
        m_queue.push(std::move(cmd));
        return;
    }
    
    if (currentState != State::Ready && currentState != State::Initializing) {
        qWarning() << "CommunicationManager: Cannot process command in state:" << currentState;
        CommandResult result = CommandResult::fromError("Card not ready");
        
        // Notify completion
        emit commandCompleted(token, result);
        
        // Wake sync waiter if any
        {
            QMutexLocker syncLocker(&m_syncMutex);
            if (m_pendingSync.contains(token)) {
                PendingSync* sync = m_pendingSync[token];
                sync->result = result;
                sync->completed = true;
                sync->condition.wakeAll();
            }
        }
        
        // Set channel to Idle on error
        if (m_queue.empty() && m_channel) {
            m_channel->setState(ChannelState::Idle);
        }
        
        return;
    }
    
    locker.unlock();
    
    // Set channel to WaitingForCard before executing command
    if (m_channel) {
        m_channel->setState(ChannelState::WaitingForCard);
    }
    
    // Execute command
    qDebug() << "CommunicationManager: Executing command:" << cmdName << "token:" << token;
    
    setState(State::Processing);
    
    CommandResult result;
    try {
        result = cmd->execute(m_commandSet.get());
    } catch (const std::exception& e) {
        qWarning() << "CommunicationManager: Command threw exception:" << e.what();
        result = CommandResult::fromError(QString("Exception: %1").arg(e.what()));
    } catch (...) {
        qWarning() << "CommunicationManager: Command threw unknown exception";
        result = CommandResult::fromError("Unknown exception");
    }
    
    setState(State::Ready);
    
    qDebug() << "CommunicationManager: Command completed:" << cmdName
             << "success:" << result.success;
    
    // Notify completion
    emit commandCompleted(token, result);
    
    // Wake sync waiter if any
    {
        QMutexLocker syncLocker(&m_syncMutex);
        if (m_pendingSync.contains(token)) {
            PendingSync* sync = m_pendingSync[token];
            sync->result = result;
            sync->completed = true;
            sync->condition.wakeAll();
        }
    }
    
    // Check if queue is empty and set channel to Idle if so
    {
        QMutexLocker queueLocker(&m_queueMutex);
        if (m_queue.empty() && m_channel) {
            m_channel->setState(ChannelState::Idle);
        }
    }
    
    // Process next command if any
    QMetaObject::invokeMethod(this, &CommunicationManager::processQueue,
                               Qt::QueuedConnection);
}

} // namespace Keycard
