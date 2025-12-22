/**
 * Example showing how to use CommunicationManager for thread-safe card operations
 * 
 * CommunicationManager is the main API for keycard-qt. It provides:
 * - Thread-safe queue-based command execution
 * - Automatic card detection and initialization
 * - Both async (signal-based) and sync (blocking) APIs
 * - Proper lifecycle management
 * 
 * This example demonstrates:
 * 1. Setting up CommunicationManager with CommandSet
 * 2. Async command execution (for UI applications)
 * 3. Sync command execution (for worker threads)
 * 4. Handling card lifecycle events
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QtConcurrent>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"

using namespace Keycard;

class KeycardApp : public QObject
{
    Q_OBJECT
    
public:
    KeycardApp(QObject* parent = nullptr) : QObject(parent)
    {
        // Step 1: Create KeycardChannel (handles platform-specific card detection)
        auto channel = std::make_shared<KeycardChannel>();
        
        // Step 2: Create CommandSet with channel and dependencies
        // For this example, we use nullptr for storage and password provider
        // In production, provide FilePairingStorage and a password callback
        m_commandSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
        
        // Step 3: Create and initialize CommunicationManager
        m_commMgr = std::make_shared<CommunicationManager>();
        if (!m_commMgr->init(m_commandSet)) {
            qCritical() << "Failed to initialize CommunicationManager!";
            QCoreApplication::exit(1);
            return;
        }
        
        // Step 4: Connect to CommunicationManager signals
        setupSignals();
        
        qDebug() << "=== CommunicationManager Example ===";
        qDebug() << "CommunicationManager initialized successfully";
        qDebug() << "";
    }
    
    ~KeycardApp()
    {
        if (m_commMgr) {
            m_commMgr->stop();
        }
    }
    
    void start()
    {
        qDebug() << "Starting card detection...";
        qDebug() << "Please insert/tap your keycard...";
        qDebug() << "";
        
        if (!m_commMgr->startDetection()) {
            qCritical() << "Failed to start card detection!";
            QCoreApplication::exit(1);
            return;
        }
        
        // Set a timeout to demonstrate both APIs
        QTimer::singleShot(30000, this, []() {
            qDebug() << "Example timeout - exiting";
            QCoreApplication::quit();
        });
    }

private slots:
    void onCardInitialized(CardInitializationResult result)
    {
        qDebug() << "========================================";
        qDebug() << "CARD INITIALIZED";
        qDebug() << "========================================";
        qDebug() << "Success:" << result.success;
        qDebug() << "Card UID:" << result.uid;
        qDebug() << "Initialized:" << result.appInfo.initialized;
        qDebug() << "Has keys:" << result.appStatus.keyInitialized;
        qDebug() << "";
        
        if (!result.success) {
            qWarning() << "Initialization failed:" << result.error;
            return;
        }
        
        // Example 1: Async API - enqueue commands that execute in background
        demonstrateAsyncAPI();
        
        // Example 2: Sync API - blocking calls (use from worker threads)
        demonstrateSyncAPI();
    }
    
    void onCardLost()
    {
        qDebug() << "========================================";
        qDebug() << "CARD REMOVED";
        qDebug() << "========================================";
        qDebug() << "Waiting for card again...";
        qDebug() << "";
    }
    
    void onCommandCompleted(QUuid token, CommandResult result)
    {
        qDebug() << "Command completed (async):";
        qDebug() << "  Token:" << token.toString();
        qDebug() << "  Success:" << result.success;
        if (!result.success) {
            qDebug() << "  Error:" << result.error;
        } else {
            qDebug() << "  Result data available:" << !result.data.isNull();
        }
        qDebug() << "";
    }
    
    void onStateChanged(CommunicationManager::State newState)
    {
        QString stateStr;
        switch (newState) {
            case CommunicationManager::State::Idle:
                stateStr = "Idle";
                break;
            case CommunicationManager::State::Initializing:
                stateStr = "Initializing";
                break;
            case CommunicationManager::State::Ready:
                stateStr = "Ready";
                break;
            case CommunicationManager::State::Processing:
                stateStr = "Processing";
                break;
        }
        qDebug() << "State changed:" << stateStr;
    }

private:
    void setupSignals()
    {
        // Card lifecycle events
        connect(m_commMgr.get(), &CommunicationManager::cardInitialized,
                this, &KeycardApp::onCardInitialized);
        
        connect(m_commMgr.get(), &CommunicationManager::cardLost,
                this, &KeycardApp::onCardLost);
        
        // Async command completion
        connect(m_commMgr.get(), &CommunicationManager::commandCompleted,
                this, &KeycardApp::onCommandCompleted);
        
        // State changes
        connect(m_commMgr.get(), &CommunicationManager::stateChanged,
                this, &KeycardApp::onStateChanged);
    }
    
    void demonstrateAsyncAPI()
    {
        qDebug() << "--- Async API Example ---";
        qDebug() << "Enqueueing SELECT command (non-blocking)...";
        
        // Create command and enqueue it
        auto cmd = std::make_unique<SelectCommand>();
        QUuid token = m_commMgr->enqueueCommand(std::move(cmd));
        
        qDebug() << "Command enqueued with token:" << token.toString();
        qDebug() << "Will receive result via commandCompleted signal";
        qDebug() << "";
        
        // The command executes in background, result comes via signal
    }
    
    void demonstrateSyncAPI()
    {
        qDebug() << "--- Sync API Example ---";
        qDebug() << "Executing command from worker thread (blocking)...";
        
        // Execute command from a worker thread (QtConcurrent pool)
        auto future = QtConcurrent::run([this]() {
            qDebug() << "Worker thread: Executing SELECT command...";
            
            auto cmd = std::make_unique<SelectCommand>();
            CommandResult result = m_commMgr->executeCommandSync(std::move(cmd), 5000);
            
            qDebug() << "Worker thread: Command completed";
            qDebug() << "  Success:" << result.success;
            if (!result.success) {
                qDebug() << "  Error:" << result.error;
            }
            qDebug() << "";
            
            return result;
        });
        
        // In real app, you might wait for result or continue with other work
    }

private:
    std::shared_ptr<CommandSet> m_commandSet;
    std::shared_ptr<CommunicationManager> m_commMgr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    KeycardApp keycardApp;
    keycardApp.start();
    
    return app.exec();
}

#include "communication_manager_example.moc"

