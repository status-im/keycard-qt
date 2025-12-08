#pragma once

#include "channel_interface.h"
#include "backends/keycard_channel_backend.h"  // For ChannelState enum
#include <QObject>
#include <QString>
#include <QByteArray>

namespace Keycard {

// Forward declaration of backend interface
class KeycardChannelBackend;

/**
 * @brief Platform-adaptive Keycard communication channel
 * 
 * This class provides a unified interface for Keycard communication
 * across different platforms using a plugin architecture:
 * 
 * - **Desktop (PC/SC)**: Direct smart card reader access via PC/SC
 *   - Windows, macOS, Linux
 *   - Automatic reader detection and polling
 *   - T=0/T=1 protocol support
 * 
 * - **Mobile (Qt NFC)**: NFC tag communication via Qt's NFC API
 *   - iOS (standard Qt NFC)
 *   - Android (Qt NFC with workarounds for Qt 6.9.x bugs)
 * 
 * The appropriate backend is automatically selected at compile time
 * based on the target platform. The backend is implemented via the
 * KeycardChannelBackend interface (see backends/keycard_channel_backend.h).
 * 
 * **Thread Safety**: This class should be used from the main thread.
 * All signals are emitted on the main thread.
 * 
 * **Example Usage**:
 * ```cpp
 * auto channel = new KeycardChannel(this);
 * connect(channel, &KeycardChannel::targetDetected, [](const QString& uid) {
 *     qDebug() << "Keycard detected:" << uid;
 * });
 * channel->startDetection();
 * ```
 */
class KeycardChannel : public QObject, public IChannel {
    Q_OBJECT
    
public:
    /**
     * @brief Create KeycardChannel with default platform backend
     * @param parent QObject parent
     * 
     * Automatically creates the appropriate backend:
     * - PC/SC on desktop (Windows, macOS, Linux)
     * - Qt NFC on iOS
     * - Android NFC or Qt NFC on Android (build-dependent)
     */
    explicit KeycardChannel(QObject* parent = nullptr);
    
    /**
     * @brief Create KeycardChannel with injected backend (for testing/DI)
     * @param backend Custom backend implementation (takes ownership)
     * @param parent QObject parent
     * 
     * Allows dependency injection of custom backends, useful for:
     * - Unit testing with mock backends
     * - Custom platform implementations
     * - Test automation without hardware
     * 
     * The channel takes ownership of the backend.
     */
    explicit KeycardChannel(KeycardChannelBackend* backend, QObject* parent = nullptr);
    
    ~KeycardChannel() override;
    
    /**
     * @brief Start detecting cards/tags
     * 
     * Begins scanning for Keycard presence:
     * - PC/SC: Polls smart card readers at regular intervals
     * - Qt NFC: Starts listening for NFC tag detection
     * 
     * Emits targetDetected() when a Keycard is found.
     */
    void startDetection();
    
    /**
     * @brief Stop detecting cards/tags
     * 
     * Stops scanning for Keycards. Does not disconnect from
     * an already connected card.
     */
    void stopDetection();
    
    /**
     * @brief Force immediate re-scan for cards
     * 
     * Triggers an immediate re-scan for cards. Useful after operations
     * that change card state (e.g., initialization, factory reset).
     * Only supported by backends that implement forceScan().
     */
    void forceScan() override;
    
    /**
     * @brief Disconnect from current target
     * 
     * Disconnects from the currently connected Keycard (if any).
     * Emits targetLost() signal.
     */
    void disconnect();
    
    /**
     * @brief Get target UID (card ID)
     * @return UID as hex string, or empty if not connected
     */
    QString targetUid() const;
    
    /**
     * @brief Get backend name for debugging
     * @return Human-readable backend name (e.g., "PC/SC", "Qt NFC")
     */
    QString backendName() const;
    
    /**
     * @brief Get the underlying backend instance
     * @return Pointer to the backend implementation
     * 
     * Allows access to platform-specific backend features.
     * Useful for iOS-specific NFC session management.
     */
    KeycardChannelBackend* backend() const { return m_backend; }
    
    /**
     * @brief Request card at app startup for initialization
     * 
     * iOS: Proactively shows NFC drawer and waits for first card tap.
     * This initializes the app with card metadata. After this, card
     * stays "connected" for subsequent operations (persistent card model).
     * 
     * Android/PC/SC: No-op (card detection already running in background).
     * 
     * @return true if card was detected, false on timeout/error
     */
    bool requestCardAtStartup();
    
    /**
     * @brief Set the channel state for lifecycle management
     * @param state The desired channel state
     * 
     * Forwards the state change to the backend implementation.
     * See KeycardChannelBackend::setState() for details.
     */
    void setState(ChannelState state);
    
    /**
     * @brief Get the current channel state
     * @return Current state
     */
    ChannelState state() const;
    
    /**
     * @brief Get the current operational channel state
     * @return Current operational state
     * 
     * This represents the actual operational state of the channel,
     * controlled by the channel implementation based on its operations.
     */
    ChannelOperationalState channelState() const;
    
    // IChannel interface implementation
    /**
     * @brief Transmit APDU command to Keycard
     * @param apdu APDU command bytes
     * @return APDU response bytes
     * @throws std::runtime_error if not connected or transmission fails
     * 
     * This is a blocking call that waits for the card's response.
     * Timeout is backend-specific (typically 5 seconds).
     */
    QByteArray transmit(const QByteArray& apdu) override;
    
    /**
     * @brief Check if connected to a Keycard
     * @return true if connected and ready for communication
     */
    bool isConnected() const override;

    
signals:
    /**
     * @brief Emitted when reader availability changes (PC/SC only)
     * @param available true if at least one reader is present, false if no readers
     * 
     * This allows UI/state management to distinguish between:
     * - No reader hardware (user needs to plug in reader)
     * - Reader present but no card (user needs to insert card)
     */
    void readerAvailabilityChanged(bool available);

    /**
     * @brief Emitted when a Keycard is detected and ready for communication
     * @param uid Unique identifier of the detected card (hex string)
     * 
     * After this signal, transmit() can be called to communicate with the card.
     */
    void targetDetected(const QString& uid);
    
    /**
     * @brief Emitted when Keycard is removed or connection is lost
     * 
     * After this signal, transmit() will fail until a new card is detected.
     */
    void targetLost();
    
    /**
     * @brief Emitted when an error occurs
     * @param message Human-readable error description
     * 
     * This includes backend initialization errors, communication errors,
     * and platform-specific issues (e.g., NFC not supported).
     */
    void error(const QString& message);
    
    /**
     * @brief Emitted when the operational channel state changes
     * @param state The new operational state
     * 
     * This signal is emitted when the channel's operational state changes
     * based on its actual operations (card detected, reading, errors, etc.).
     * This is independent of the lifecycle state set via setState().
     */
    void channelStateChanged(ChannelOperationalState state);
    
private:
    /**
     * @brief Create default platform backend
     * @return Newly created backend instance
     * 
     * Factory method that creates the appropriate backend based on platform.
     * Called by the default constructor.
     */
    KeycardChannelBackend* createDefaultBackend();
    
    /**
     * @brief Backend instance selected at compile time or injected
     * 
     * Pointer to the platform-specific backend implementation:
     * - KeycardChannelPcsc on desktop
     * - KeycardChannelQtNfc on mobile
     * - Custom backend (via DI constructor)
     */
    KeycardChannelBackend* m_backend;
    
    QString m_targetUid;  // Cached UID for quick access
    bool m_ownsBackend;    // true if we created the backend, false if injected
};

} // namespace Keycard
