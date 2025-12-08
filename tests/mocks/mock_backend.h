// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard-qt/backends/keycard_channel_backend.h"
#include <QByteArray>
#include <QTimer>
#include <QQueue>

namespace Keycard {
namespace Test {

/**
 * @brief Mock backend for testing KeycardChannel without hardware
 * 
 * Features:
 * - Simulates card detection/removal
 * - Configurable APDU responses
 * - Automatic or manual card connection
 * - Error simulation
 * - Signal emission testing
 * 
 * Example:
 * @code
 * auto* mock = new MockBackend();
 * mock->setAutoConnect(true);
 * mock->queueResponse(QByteArray::fromHex("9000"));  // Success
 * 
 * auto* channel = new KeycardChannel(mock);
 * channel->startDetection();  // Will auto-connect
 * QByteArray response = channel->transmit(apdu);  // Returns queued response
 * @endcode
 */
class MockBackend : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit MockBackend(QObject* parent = nullptr);
    ~MockBackend() override;

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    bool isConnected() const override;
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return "Mock Backend"; }
    bool requestCardAtStartup() override { return true; } // No-op for mock
    void setState(ChannelState state) override;
    ChannelState state() const override { return m_state; }
    void forceScan() override;

    // ========================================================================
    // Configuration Methods
    // ========================================================================

    /**
     * @brief Enable/disable automatic card connection after startDetection()
     * @param autoConnect If true, emits targetDetected after small delay
     */
    void setAutoConnect(bool autoConnect);

    /**
     * @brief Set card UID for targetDetected signal
     * @param uid Card UID as hex string
     */
    void setCardUid(const QString& uid);

    /**
     * @brief Queue an APDU response
     * @param response APDU response (including SW1SW2)
     * 
     * Queued responses are returned by transmit() in FIFO order.
     * If queue is empty, returns default response (9000).
     */
    void queueResponse(const QByteArray& response);

    /**
     * @brief Clear all queued responses
     */
    void clearResponses();

    /**
     * @brief Set default response when queue is empty
     * @param response Default APDU response
     */
    void setDefaultResponse(const QByteArray& response);

    /**
     * @brief Enable/disable logging of transmitted APDUs
     * @param log If true, logs all APDUs via qDebug()
     */
    void setLogApdu(bool log) { m_logApdu = log; }

    // ========================================================================
    // Simulation Control
    // ========================================================================

    /**
     * @brief Manually simulate card insertion
     * 
     * Emits targetDetected() signal with configured UID.
     * Sets isConnected() to true.
     */
    void simulateCardInserted();

    /**
     * @brief Manually simulate card removal
     * 
     * Emits cardRemoved() signal.
     * Sets isConnected() to false.
     */
    void simulateCardRemoved();

    /**
     * @brief Manually simulate an error
     * @param errorMessage Error message to emit
     */
    void simulateError(const QString& errorMessage);

    /**
     * @brief Make next transmit() call throw an exception
     * @param errorMessage Exception message
     */
    void setNextTransmitThrows(const QString& errorMessage);

    // ========================================================================
    // Inspection Methods (for test assertions)
    // ========================================================================

    /**
     * @brief Get list of all transmitted APDUs
     * @return List of APDU commands sent via transmit()
     */
    QList<QByteArray> getTransmittedApdus() const { return m_transmittedApdus; }

    /**
     * @brief Get count of transmitted APDUs
     */
    int getTransmitCount() const { return m_transmittedApdus.size(); }

    /**
     * @brief Get last transmitted APDU
     * @return Last APDU or empty if none transmitted
     */
    QByteArray getLastTransmittedApdu() const {
        return m_transmittedApdus.isEmpty() ? QByteArray() : m_transmittedApdus.last();
    }

    /**
     * @brief Check if detection is currently active
     */
    bool isDetecting() const { return m_detecting; }

    /**
     * @brief Get polling interval
     */
    int getPollingInterval() const { return m_pollingInterval; }

    /**
     * @brief Reset mock state
     * 
     * Clears queued responses, transmitted APDUs, disconnects, stops detection.
     */
    void reset();

private:
    // State
    bool m_autoConnect;
    bool m_connected;
    bool m_detecting;
    QString m_cardUid;
    int m_pollingInterval;
    bool m_logApdu;
    ChannelState m_state = ChannelState::Idle;

    // Response queue
    QQueue<QByteArray> m_responseQueue;
    QByteArray m_defaultResponse;

    // Tracking
    QList<QByteArray> m_transmittedApdus;

    // Error simulation
    QString m_nextThrowMessage;

    // Auto-connect timer
    QTimer* m_autoConnectTimer;
};

} // namespace Test
} // namespace Keycard

