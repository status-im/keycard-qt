#ifndef I_COMMUNICATION_MANAGER_H
#define I_COMMUNICATION_MANAGER_H

#include "types.h"
#include "card_command.h"
#include "command_set.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <memory>

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
    QByteArray metadataTlv; // The caller should parse the metadataTlv, since the binary format is project-specific.
    bool hasMetadata;

    CardInitializationResult() : success(false), hasMetadata(false) {}

    static CardInitializationResult fromSuccess(const QString& cardUid, const ApplicationInfo& info, const ApplicationStatus& status,
                                                const QByteArray& metadata = QByteArray(), bool metadataAvailable = false) {
        CardInitializationResult result;
        result.success = true;
        result.uid = cardUid;
        result.appInfo = info;
        result.appStatus = status;
        result.metadataTlv = metadata;
        result.hasMetadata = metadataAvailable;
        return result;
    }

    // appInfo/appStatus may be passed when SELECT has already succeeded but a later
    // step (pairing, secure channel) failed — so the caller can still expose
    // SELECT-derived fields (instanceUID, version, availableSlots, keyUID).
    static CardInitializationResult fromError(const QString& err,
                                              const ApplicationInfo& info = ApplicationInfo(),
                                              const ApplicationStatus& status = ApplicationStatus()) {
        CardInitializationResult result;
        result.success = false;
        result.error = err;
        result.appInfo = info;
        result.appStatus = status;
        return result;
    }
};

/**
 * @brief Abstract interface for CommunicationManager
 * 
 * This interface allows for mocking and testing of components that depend
 * on CommunicationManager without requiring the full threading infrastructure.
 * 
 * Both the real CommunicationManager and test mocks inherit from this class.
 */
class ICommunicationManager : public QObject {
    Q_OBJECT
    
public:
    explicit ICommunicationManager(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~ICommunicationManager();
    
    // Detection management
    virtual bool startDetection() = 0;
    virtual void stopDetection() = 0;
    virtual void cancelPendingOperations(const QString& reason) = 0;
    
    // Command execution
    virtual CommandResult executeCommandSync(std::unique_ptr<CardCommand> cmd) = 0;
    
    // Card information
    virtual ApplicationInfo applicationInfo() const = 0;
    virtual ApplicationStatus applicationStatus() const = 0;
    
    // Batch operations
    virtual void startBatchOperations() = 0;
    virtual void endBatchOperations() = 0;
    
    // Access to command set (may return nullptr for mocks)
    virtual std::shared_ptr<CommandSet> commandSet() const = 0;
    
signals:
    /**
     * @brief Emitted when card initialization completes
     */
    void cardInitialized(const CardInitializationResult& result);
    
    /**
     * @brief Emitted when card is lost/removed
     */
    void cardLost();
    
    /**
     * @brief Emitted when state changes (optional - for diagnostics)
     */
    void stateChanged(int newState);

    /**
     * @brief Emitted when an operation is cancelled
     * @param reason Reason for cancellation
     */
    void operationCancelled(const QString& reason);
};

} // namespace Keycard

// Declare metatype for signal parameters
Q_DECLARE_METATYPE(Keycard::CardInitializationResult)

#endif // I_COMMUNICATION_MANAGER_H

