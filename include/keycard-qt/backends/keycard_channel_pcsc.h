// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard_channel_backend.h"
#include <QThread>
#include <QString>
#include <QStringList>
#include <QAtomicInt>
#include <QMutex>

namespace Keycard {

// Forward declarations to hide PC/SC types from MOC
struct PcscState;

/**
 * @brief PC/SC backend for desktop smart card readers
 * 
 * Implements communication with Keycard via PC/SC (Personal Computer/Smart Card)
 * standard, used on desktop platforms (Windows, macOS, Linux).
 * 
 * Features:
 * - Event-driven card detection (matches status-keycard-go)
 * - Uses SCardGetStatusChange() for efficient monitoring
 * - T=1 protocol support
 * - APDU transmission with proper error handling
 * 
 * Requirements:
 * - PC/SC daemon running (pcscd on Linux/macOS, built-in on Windows)
 * - Smart card reader hardware
 */
class KeycardChannelPcsc : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit KeycardChannelPcsc(QObject* parent = nullptr);
    ~KeycardChannelPcsc() override;

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    bool isConnected() const override;
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return "PC/SC"; }
    void setState(ChannelState state) override;
    ChannelState state() const override { return m_state; }

    /**
     * @brief Force immediate re-scan for cards (used after init/factory reset)
     * Matches status-keycard-go's forceScan mechanism
     */
    void forceScan() override;

private:
    /**
     * @brief Establish PC/SC context for communication
     */
    void establishContext();

    /**
     * @brief Release PC/SC context
     */
    void releaseContext();

    /**
     * @brief List all available smart card readers
     * @return List of reader names
     */
    QStringList listReaders();

    /**
     * @brief Connect to a specific reader
     * @param readerName Name of the reader to connect to
     * @return true if connection successful
     */
    bool connectToReader(const QString& readerName);

    /**
     * @brief Disconnect from the current reader
     */
    void disconnectFromCard();

    /**
     * @brief Get ATR (Answer To Reset) from connected card
     * @return ATR bytes
     */
    QByteArray getATR();
    
    /**
     * @brief Event-driven detection loop (runs in separate thread)
     * Matches status-keycard-go's waitForCard pattern
     */
    void detectionLoop();
    
    /**
     * @brief Watch for card removal (Phase 2 of two-phase detection)
     * Monitors specific reader until card is removed or force scan requested
     * @param readerName Name of reader to watch
     */
    void watchCardRemoval(const QString& readerName);

    // PC/SC state (hidden via pimpl to avoid MOC issues with PC/SC types)
    PcscState* m_pcscState;
    bool m_connected;

    // Event-driven card detection
    QThread* m_detectionThread;
    QAtomicInt m_stopDetection;
    QAtomicInt m_forceScan;  // Trigger for force scan (matches status-keycard-go)
    QString m_lastDetectedReader;
    QString m_lastDetectedUid;  // Track to prevent duplicate events
    QByteArray m_lastATR;
    bool m_lastReaderAvailable;  // Track reader availability changes
    bool m_firstReaderCheck;     // Track if we've done initial reader check
    
    // Thread safety - protects transmit() to prevent APDU corruption
    mutable QMutex m_transmitMutex;
    
    // Channel state (state-driven architecture)
    ChannelState m_state = ChannelState::Idle;
};

} // namespace Keycard