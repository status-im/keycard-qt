/**
 * Advanced example showing real-world keycard operations
 * 
 * This example demonstrates:
 * 1. Card initialization with PIN/PUK
 * 2. PIN verification
 * 3. Key generation
 * 4. Signing transactions
 * 5. Error handling and retry logic
 * 6. Batch operations (multiple commands without closing channel)
 */

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include "keycard-qt/communication_manager.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/card_command.h"

using namespace Keycard;

class AdvancedKeycardApp : public QObject
{
    Q_OBJECT
    
public:
    AdvancedKeycardApp(QObject* parent = nullptr) : QObject(parent)
    {
        // Create channel and command set
        auto channel = std::make_shared<KeycardChannel>();
        m_commandSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
        
        // Create and initialize CommunicationManager
        m_commMgr = std::make_shared<CommunicationManager>();
        if (!m_commMgr->init(m_commandSet)) {
            qCritical() << "Failed to initialize CommunicationManager!";
            QCoreApplication::exit(1);
            return;
        }
        
        // Connect signals
        connect(m_commMgr.get(), &CommunicationManager::cardInitialized,
                this, &AdvancedKeycardApp::onCardInitialized);
        
        connect(m_commMgr.get(), &CommunicationManager::cardLost,
                this, &AdvancedKeycardApp::onCardLost);
        
        qDebug() << "=== Advanced Keycard Operations Example ===";
        qDebug() << "";
    }
    
    ~AdvancedKeycardApp()
    {
        if (m_commMgr) {
            m_commMgr->stop();
        }
    }
    
    void start()
    {
        qDebug() << "Starting card detection...";
        qDebug() << "Insert a card to see advanced operations";
        qDebug() << "";
        
        m_commMgr->startDetection();
        
        // Timeout after 1 minute
        QTimer::singleShot(60000, this, []() {
            qDebug() << "Example timeout - exiting";
            QCoreApplication::quit();
        });
    }

private slots:
    void onCardInitialized(CardInitializationResult result)
    {
        qDebug() << "========================================";
        qDebug() << "Card detected and initialized";
        qDebug() << "========================================";
        
        if (!result.success) {
            qWarning() << "Card initialization failed:" << result.error;
            return;
        }
        
        qDebug() << "Card UID:" << result.uid;
        qDebug() << "App Version:" << result.appInfo.appVersion;
        qDebug() << "Initialized:" << result.appInfo.initialized;
        qDebug() << "Has keys:" << result.appStatus.keyInitialized;
        qDebug() << "PIN retry count:" << result.appStatus.pinRetryCount;
        qDebug() << "PUK retry count:" << result.appStatus.pukRetryCount;
        qDebug() << "";
        
        if (!result.appInfo.initialized) {
            demonstrateInitialization();
        } else if (!result.appStatus.keyInitialized) {
            demonstrateKeyGeneration();
        } else {
            demonstrateSigning();
        }
    }
    
    void onCardLost()
    {
        qDebug() << "Card removed - waiting for next card...";
        qDebug() << "";
    }

private:
    /**
     * Initialize a new card with PIN, PUK, and pairing password
     */
    void demonstrateInitialization()
    {
        qDebug() << "--- Card Initialization Example ---";
        qDebug() << "Initializing empty card...";
        
        QString pin = "000000";          // 6-digit PIN
        QString puk = "123456789012";    // 12-digit PUK
        QString pairingPassword = "KeycardDefaultPairing";
        
        auto cmd = std::make_unique<InitCommand>(pin, puk, pairingPassword);
        CommandResult result = m_commMgr->executeCommandSync(std::move(cmd), 60000);
        
        if (result.success) {
            qDebug() << "✅ Card initialized successfully!";
            qDebug() << "PIN:" << pin;
            qDebug() << "PUK:" << puk;
            qDebug() << "";
            
            // After init, we can generate keys
            demonstrateKeyGeneration();
        } else {
            qWarning() << "❌ Initialization failed:" << result.error;
        }
    }
    
    /**
     * Generate keys on the card
     */
    void demonstrateKeyGeneration()
    {
        qDebug() << "--- Key Generation Example ---";
        
        // First, verify PIN
        qDebug() << "Verifying PIN...";
        auto verifyCmd = std::make_unique<VerifyPINCommand>("000000");
        CommandResult verifyResult = m_commMgr->executeCommandSync(std::move(verifyCmd), 30000);
        
        if (!verifyResult.success) {
            qWarning() << "❌ PIN verification failed:" << verifyResult.error;
            return;
        }
        qDebug() << "✅ PIN verified";
        
        // Generate BIP39 mnemonic on card
        qDebug() << "Generating mnemonic on card...";
        auto generateCmd = std::make_unique<GenerateMnemonicCommand>(8); // 24 words
        CommandResult generateResult = m_commMgr->executeCommandSync(std::move(generateCmd), 60000);
        
        if (generateResult.success) {
            qDebug() << "✅ Mnemonic generated!";
            qDebug() << "Word indices:" << generateResult.data;
            qDebug() << "";
            qDebug() << "Card now has keys and is ready for signing";
            qDebug() << "";
        } else {
            qWarning() << "❌ Key generation failed:" << generateResult.error;
        }
    }
    
    /**
     * Demonstrate batch operations - multiple commands without closing channel
     */
    void demonstrateBatchOperations()
    {
        qDebug() << "--- Batch Operations Example ---";
        qDebug() << "Starting batch mode (channel stays open)...";
        
        m_commMgr->startBatchOperations();
        
        // Verify PIN
        auto verifyCmd = std::make_unique<VerifyPINCommand>("000000");
        CommandResult result1 = m_commMgr->executeCommandSync(std::move(verifyCmd), 5000);
        qDebug() << "PIN verify:" << (result1.success ? "✅" : "❌");
        
        // Get application status
        auto statusCmd = std::make_unique<GetStatusCommand>();
        CommandResult result2 = m_commMgr->executeCommandSync(std::move(statusCmd), 5000);
        qDebug() << "Get status:" << (result2.success ? "✅" : "❌");
        
        // Get status
        auto selectCmd = std::make_unique<SelectCommand>();
        CommandResult result3 = m_commMgr->executeCommandSync(std::move(selectCmd), 5000);
        qDebug() << "Get status:" << (result3.success ? "✅" : "❌");
        
        m_commMgr->endBatchOperations();
        qDebug() << "Batch operations complete";
        qDebug() << "";
    }
    
    /**
     * Sign a transaction hash
     */
    void demonstrateSigning()
    {
        qDebug() << "--- Signing Example ---";
        
        // Verify PIN first
        qDebug() << "Verifying PIN...";
        auto verifyCmd = std::make_unique<VerifyPINCommand>("000000");
        CommandResult verifyResult = m_commMgr->executeCommandSync(std::move(verifyCmd), 30000);
        
        if (!verifyResult.success) {
            qWarning() << "❌ PIN verification failed:" << verifyResult.error;
            qDebug() << "Remaining attempts:" << verifyResult.data.toMap()["remainingAttempts"].toInt();
            return;
        }
        qDebug() << "✅ PIN verified";
        
        // Demonstrate batch operations for efficiency
        demonstrateBatchOperations();
        
        // Sign a transaction
        qDebug() << "Signing transaction...";
        
        // Example transaction hash (32 bytes)
        QByteArray hash = QByteArray::fromHex(
            "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"
        );
        
        QString derivationPath = "m/44'/60'/0'/0/0"; // Ethereum standard path
        
        auto signCmd = std::make_unique<SignCommand>(hash, derivationPath);
        CommandResult signResult = m_commMgr->executeCommandSync(std::move(signCmd), 30000);
        
        if (signResult.success) {
            qDebug() << "✅ Signature generated!";
            QVariantMap sigData = signResult.data.toMap();
            qDebug() << "Public key:" << sigData["publicKey"].toByteArray().toHex();
            qDebug() << "Signature available";
            qDebug() << "";
        } else {
            qWarning() << "❌ Signing failed:" << signResult.error;
        }
    }

private:
    std::shared_ptr<CommandSet> m_commandSet;
    std::shared_ptr<CommunicationManager> m_commMgr;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    AdvancedKeycardApp keycardApp;
    keycardApp.start();
    
    return app.exec();
}

#include "advanced_operations_example.moc"

