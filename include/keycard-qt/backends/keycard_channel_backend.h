// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QByteArray>

namespace Keycard {

/**
 * @brief Channel state representing the lifecycle of card communication
 * 
 * This enum models the explicit states a channel can be in, allowing
 * platform-specific behavior (e.g., iOS NFC drawer management) to be
 * handled cleanly without leaking implementation details to higher layers.
 */
enum class ChannelState {
    /**
     * @brief Channel is idle, not expecting card interaction
     * 
     * - iOS: No NFC session active
     * - Android/PC/SC: Continuous detection active
     */
    Idle,
    
    /**
     * @brief Channel is waiting for user to present card
     * 
     * - iOS: NFC session active, drawer visible to user
     * - Android/PC/SC: No change (already detecting)
     */
    WaitingForCard,
};

/**
 * @brief Operational state of the channel from the channel's perspective
 * 
 * This enum represents the actual operational state of the channel,
 * independent of the lifecycle state set via setState(). The channel
 * implementation controls this state based on its actual operations.
 */
enum class ChannelOperationalState {
    /**
     * @brief Channel is idle, not actively doing anything
     */
    Idle,
    
    /**
     * @brief Channel is waiting for a keycard to be presented
     */
    WaitingForKeycard,
    
    /**
     * @brief Channel is actively reading/communicating with a keycard
     */
    Reading,
    
    /**
     * @brief An error occurred during channel operation
     */
    Error,
    /**
     * @brief There's no NFC/PCSC HW available
     */
    NotSupported,
    /**
     * @brief NFC/PCSC HW available, but disabled
    */
    NotAvailable
};

/**
 * @brief Abstract interface for Keycard communication backends
 * 
 * This interface defines the contract that all backend implementations
 * (PC/SC, Qt NFC, etc.) must follow. Backends handle platform-specific
 * communication with smart cards/NFC tags.
 * 
 * Backend Selection:
 * - PC/SC: Desktop platforms (Windows, macOS, Linux) via smart card readers
 * - Qt NFC: Mobile platforms (iOS, Android) via NFC
 * 
 * Thread Safety:
 * Implementations should be thread-safe or clearly document threading requirements.
 */
class KeycardChannelBackend : public QObject
{
    Q_OBJECT

public:
    explicit KeycardChannelBackend(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~KeycardChannelBackend() = default;

    /**
     * @brief Start detection/scanning for cards/tags
     * 
     * For PC/SC: Start polling for smart card readers
     * For Qt NFC: Start listening for NFC tag detection
     */
    virtual void startDetection() = 0;

    /**
     * @brief Stop detection/scanning
     */
    virtual void stopDetection() = 0;

    /**
     * @brief Disconnect from the currently connected card/tag
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if currently connected to a card/tag
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Transmit APDU command and receive response
     * @param apdu The APDU command to send
     * @return Response APDU from the card
     * @throws std::runtime_error if transmission fails
     */
    virtual QByteArray transmit(const QByteArray& apdu) = 0;

    /**
     * @brief Get backend name for logging/debugging
     * @return Human-readable backend name (e.g., "PC/SC", "Qt NFC")
     */
    virtual QString backendName() const = 0;
    
    /**
     * @brief Set the channel state for lifecycle management
     * @param state The desired channel state
     * 
     * This method allows high-level code to manage the channel lifecycle
     * in a platform-agnostic way. Each backend interprets states according
     * to its platform's requirements:
     * 
     * - iOS: Controls NFC session and drawer visibility
     * - Android/PC/SC: May be no-op if continuous detection is used
     * 
     * State transitions:
     * - Idle → WaitingForCard: Start looking for card
     * - WaitingForCard → CardPresent: Card detected
     * - Any → Idle: Clean up and stop
     */
    virtual void setState(ChannelState state) = 0;
    
    /**
     * @brief Get the current channel state
     * @return Current state
     */
    virtual ChannelState state() const = 0;
    
    /**
     * @brief Get the current operational channel state
     * @return Current operational state
     * 
     * This represents the actual operational state of the channel,
     * controlled by the channel implementation based on its operations.
     */
    virtual ChannelOperationalState channelState() const { return ChannelOperationalState::Idle; }

    /**
     * @brief Force immediate re-scan for cards
     * 
     * Triggers an immediate re-scan for cards. Useful after operations
     * that change card state (e.g., initialization, factory reset).
     */
    virtual void forceScan() = 0;

signals:
    /**
     * @brief Emitted when reader availability changes (PC/SC only)
     * @param available true if at least one reader is present, false if no readers
     * 
     * This signal allows proper state tracking:
     * - No readers → WaitingForReader state
     * - Reader present → WaitingForCard state  
     * - Card detected → ConnectingCard state
     */
    void readerAvailabilityChanged(bool available);

    /**
     * @brief Emitted when a card/tag is detected and ready for communication
     * @param uid Unique identifier of the detected card/tag (hex string)
     */
    void targetDetected(const QString& uid);

    /**
     * @brief Emitted when a card/tag is removed or connection lost
     */
    void cardRemoved();

    /**
     * @brief Emitted when an error occurs
     * @param message Error description
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
};

} // namespace Keycard

