#pragma once

#include "card_command.h"
#include "command_set.h"
#include "keycard_channel.h"
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QHash>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <memory>
#include <queue>

namespace Keycard {

/**
 * @brief Result of card initialization sequence
 */
struct CardInitializationResult {
    bool success;
    QString error;
    QString uid;  // Card UID
    ApplicationInfo appInfo;
    ApplicationStatus appStatus;
    
    CardInitializationResult() : success(false) {}
    
    static CardInitializationResult fromSuccess(const QString& cardUid, const ApplicationInfo& info, const ApplicationStatus& status) {
        CardInitializationResult result;
        result.success = true;
        result.uid = cardUid;
        result.appInfo = info;
        result.appStatus = status;
        return result;
    }
    
    static CardInitializationResult fromError(const QString& err) {
        CardInitializationResult result;
        result.success = false;
        result.error = err;
        return result;
    }
};

/**
 * @brief Manages card communication with queue-based architecture
 * 
 * This is the central component that solves the race condition problem by:
 * 1. Running all card I/O on a single dedicated thread
 * 2. Serializing all operations through a command queue
 * 3. Ensuring initialization completes before processing commands
 * 4. Providing both async (signal-based) and sync (blocking) APIs
 * 
 * Architecture inspired by status-keycard-go's transmitChannel pattern,
 * adapted to Qt's event loop and signal/slot mechanism.
 * 
 * Thread Safety:
 * - All public methods are thread-safe
 * - Commands can be enqueued from any thread
 * - Execution happens on dedicated communication thread
 * - Results delivered via signals (async) or return values (sync)
 */
class CommunicationManager : public QObject {
    Q_OBJECT
    
public:
    /**
     * @brief Communication state
     */
    enum class State {
        Idle,           // No card present or not started
        Initializing,   // Card detected, running initialization sequence
        Ready,          // Card initialized and ready for commands
        Processing      // Executing a command
    };
    Q_ENUM(State)
    
    explicit CommunicationManager(QObject* parent = nullptr);
    ~CommunicationManager() override;
    
    /**
     * @brief Initialize the communication manager
     * @param commandSet CommandSet instance that owns the channel
     * @return true on success
     * 
     * This is the new, cleaner initialization method where CommandSet
     * owns the channel and manages all card detection signals.
     * 
     * Benefits:
     * - No race conditions (CommandSet handles detection first)
     * - Proper encapsulation (channel ownership)
     * - Thread-safe (CommandSet on main thread, manager on comm thread)
     * 
     * This creates the communication thread and connects to CommandSet
     * signals (cardReady, cardLost), but does NOT start card detection.
     * Call startDetection() to begin monitoring for cards.
     */
    bool init(std::shared_ptr<CommandSet> commandSet);
    
    
    /**
     * @brief Start card detection
     * @return true on success
     * 
     * Begins monitoring for card presence. Must be called after init().
     * Can be called multiple times (e.g., to resume after stop).
     */
    bool startDetection();
    
    /**
     * @brief Stop card detection
     * 
     * Stops monitoring for cards but keeps the communication thread alive.
     * Queue processing continues for enqueued commands.
     * Call startDetection() to resume monitoring.
     */
    void stopDetection();
    
    /**
     * @brief Stop the communication manager completely
     * 
     * Stops detection, the communication thread, and cleans up resources.
     * Any pending commands will be cancelled.
     */
    void stop();
    
    /**
     * @brief Enqueue command for async execution
     * @param cmd Command to execute
     * @return Token for tracking completion
     * 
     * The command is added to the queue and will be executed when:
     * 1. Card is in Ready state (or command allows init state)
     * 2. No other command is currently executing
     * 
     * Completion is signaled via commandCompleted().
     */
    QUuid enqueueCommand(std::unique_ptr<CardCommand> cmd);
    
    /**
     * @brief Execute command synchronously (blocking)
     * @param cmd Command to execute
     * @param timeoutMs Timeout in milliseconds
     * @return CommandResult with success/failure and data
     * 
     * This is a convenience wrapper around enqueueCommand() that blocks
     * until the command completes or times out.
     * 
     * IMPORTANT: Do NOT call this from the communication thread or main thread
     * if the main thread needs to process events. Use from worker threads only.
     */
    CommandResult executeCommandSync(std::unique_ptr<CardCommand> cmd, int timeoutMs = -1);
    
    /**
     * @brief Get current state
     */
    State state() const;
    
    /**
     * @brief Get current card info (only valid when Ready)
     */
    ApplicationInfo applicationInfo() const;
    
    /**
     * @brief Get current card status (only valid when Ready)
     */
    ApplicationStatus applicationStatus() const;
    
    /**
     * @brief Get raw data from card (for metadata operations)
     * @param type Data type (e.g., 0x00 for public data)
     * @return Raw data from card
     */
    QByteArray getDataFromCard(uint8_t type);
    
    /**
     * @brief Store raw data to card (for metadata operations)
     * @param type Data type (e.g., 0x00 for public data)
     * @param data Data to store
     * @return true on success
     */
    bool storeDataToCard(uint8_t type, const QByteArray& data);
    
signals:
    /**
     * @brief Emitted when a command completes
     * @param token Command token
     * @param result Command result
     */
    void commandCompleted(QUuid token, CommandResult result);
    
    /**
     * @brief Emitted when card initialization completes
     * @param result Initialization result
     */
    void cardInitialized(CardInitializationResult result);
    
    /**
     * @brief Emitted when card is lost/removed
     */
    void cardLost();
    
    /**
     * @brief Emitted when state changes
     * @param newState New state
     */
    void stateChanged(State newState);
    
private slots:
    /**
     * @brief Handle card ready signal from CommandSet
     * @param uid Card UID
     * 
     * Called when CommandSet has finished processing card detection:
     * - Secure channel has been reset (if re-detection)
     * - SELECT has been executed successfully
     * - Card is ready for commands
     * 
     * This runs on the communication thread (queued connection).
     */
    void onCardReady(const QString& uid);
    
    /**
     * @brief Handle card lost signal from CommandSet
     * 
     * Called when CommandSet detects card removal.
     * Secure channel has already been reset by CommandSet.
     */
    void onCardLost();
    
    /**
     * @brief Handle channel state changed from CommandSet
     * @param state New channel state
     * 
     * Allows tracking of channel state for operational state emission.
     */
    void onChannelStateChanged(ChannelState state);
    
    /**
     * @brief Process next command in queue
     * 
     * Called automatically when:
     * - A new command is enqueued
     * - Previous command completes
     * - Card initialization completes
     */
    void processQueue();
    
private:
    /**
     * @brief Communication thread worker
     * 
     * This thread runs the Qt event loop and processes all card operations.
     * Similar to Go's cardCommunicationRoutine with runtime.LockOSThread().
     */
    class CommunicationThread : public QThread {
    public:
        explicit CommunicationThread(CommunicationManager* /*manager*/) {}
        
    protected:
        void run() override;
    };
    
    /**
     * @brief Initialize card sequence (runs on communication thread)
     * 
     * This is the equivalent of Go's connectKeycard() method.
     * It performs the full initialization atomically:
     * 1. SELECT applet
     * 2. Check if initialized
     * 3. Ensure pairing (load or pair)
     * 4. Open secure channel
     * 5. Get status
     * 6. Get metadata (optional)
     * 
     * NO OTHER OPERATIONS can run concurrently with this.
     */
    CardInitializationResult initializeCardSequence();
    
    /**
     * @brief Set state and emit signal
     */
    void setState(State newState);
    
    // Thread and queue management
    CommunicationThread* m_commThread;
    std::queue<std::unique_ptr<CardCommand>> m_queue;  // std::queue supports move-only types
    mutable QMutex m_queueMutex;
    QWaitCondition m_queueNotEmpty;
    
    // Synchronous execution support
    struct PendingSync {
        QWaitCondition condition;
        CommandResult result;
        bool completed;
    };
    
    // Use shared_ptr to prevent dangling pointers
    // When executeCommandSync() returns, the PendingSync object remains valid
    // until the communication thread finishes accessing it
    QHash<QUuid, std::shared_ptr<PendingSync>> m_pendingSync;
    QMutex m_syncMutex;
    
    // State
    State m_state;
    mutable QMutex m_stateMutex;
    QString m_currentCardUID;
    
    // Card components (accessed only from communication thread)
    // CommandSet owns channel, pairing storage, and password provider
    std::shared_ptr<CommandSet> m_commandSet;
    
    // Cached card info
    ApplicationInfo m_appInfo;
    ApplicationStatus m_appStatus;
    mutable QMutex m_infoMutex;
    
    // Running flag
    bool m_running;
};

} // namespace Keycard
