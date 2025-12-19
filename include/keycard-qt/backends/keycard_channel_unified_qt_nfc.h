// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard_channel_backend.h"
#include <QNearFieldManager>
#include <QNearFieldTarget>
#include <QList>
#include <QMutex>
#include <QEventLoop>

namespace Keycard {

/**
 * @brief Unified Qt NFC backend for Android, iOS, and Desktop (PC/SC)
 * 
 * Implements communication with Keycard via Qt's NFC API across all platforms.
 *
 */
class KeycardChannelUnifiedQtNfc : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit KeycardChannelUnifiedQtNfc(QObject* parent = nullptr);
    ~KeycardChannelUnifiedQtNfc() override;

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    void disconnect() override;
    bool isConnected() const override;
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return "Qt NFC (Unified)"; }
    void setState(ChannelState state) override;
    ChannelState state() const override { return m_state; }
    ChannelOperationalState channelState() const override { return m_channelState; }
    void forceScan() override;

private slots:
    void onTargetDetected(QNearFieldTarget* target);
    void onTargetLost(QNearFieldTarget* target);

private:
    void setupTargetSignals(QNearFieldTarget* target);
    QString describe(QNearFieldTarget::Error error);
    bool isTargetStillValid() const;  // Check if target is truly usable (not stale)
    
    // Qt NFC core
    QNearFieldManager* m_manager;
    QNearFieldTarget* m_target;
    bool m_targetIsStale = false;  // Track if Android tag has been invalidated
    
    // State management
    ChannelState m_state = ChannelState::Idle;
    ChannelOperationalState m_channelState = ChannelOperationalState::Idle;
    bool m_detectionActive = false;
    
    // Helper to update and emit channel state
    void updateChannelState(ChannelOperationalState newState);
    
    // Thread safety
    mutable QMutex m_transmitMutex;
    
    // Pending APDU requests
    struct PendingRequest {
        QNearFieldTarget::RequestId requestId;
        QEventLoop* eventLoop = nullptr;
        QByteArray response;
        bool completed = false;
    };
    QList<PendingRequest*> m_pendingRequests;
};

} // namespace Keycard

