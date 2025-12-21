// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_unified_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>

namespace Keycard {

// Android NFC timeout extension is now handled via platform/android_nfc_utils.h
// and called from CommandSet before long operations like GlobalPlatform factory reset

KeycardChannelUnifiedQtNfc::KeycardChannelUnifiedQtNfc(QObject* parent)
    : KeycardChannelBackend(parent)
    , m_manager(nullptr)
    , m_target(nullptr)
{
    m_manager = new QNearFieldManager(this);
    
    connect(m_manager, &QNearFieldManager::targetDetected,
            this, &KeycardChannelUnifiedQtNfc::onTargetDetected, Qt::DirectConnection);
    connect(m_manager, &QNearFieldManager::targetLost,
            this, &KeycardChannelUnifiedQtNfc::onTargetLost, Qt::DirectConnection);
}

KeycardChannelUnifiedQtNfc::~KeycardChannelUnifiedQtNfc()
{
    stopDetection();
    disconnect();
}

void KeycardChannelUnifiedQtNfc::startDetection()
{
    qDebug() << "KeycardChannelUnifiedQtNfc::startDetection()";

    if (!m_manager->isSupported(QNearFieldTarget::TagTypeSpecificAccess)) {
        updateChannelState(ChannelOperationalState::NotSupported);
        emit readerAvailabilityChanged(false);
        emit error("NFC not supported on this platform");
        m_detectionActive = false;
        return;
    }

    if (!m_manager->isEnabled()) {
        updateChannelState(ChannelOperationalState::NotAvailable);
        emit readerAvailabilityChanged(false);
        emit error("NFC/PCSC is disabled");
        m_detectionActive = false;
        return;
    }
    // Desktop: Start continuous detection immediately
    m_manager->startTargetDetection(QNearFieldTarget::TagTypeSpecificAccess);
    m_detectionActive = true;
    emit readerAvailabilityChanged(true);
    updateChannelState(ChannelOperationalState::WaitingForKeycard);
}

void KeycardChannelUnifiedQtNfc::stopDetection()
{
#ifdef Q_OS_IOS
    QMutexLocker locker(&m_transmitMutex);
    qDebug() << "KeycardChannelUnifiedQtNfc::stopDetection()";
    if (m_detectionActive) {
        m_manager->stopTargetDetection();
        m_detectionActive = false;
    }
#endif
    updateChannelState(ChannelOperationalState::Idle);
}

void KeycardChannelUnifiedQtNfc::setState(ChannelState state)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::setState() called with state:" << static_cast<int>(state) << "| current state:" << static_cast<int>(m_state);
    m_state = state;
    switch (state) {
        case ChannelState::Idle:
            stopDetection();
            break;
            
        case ChannelState::WaitingForCard:
            startDetection();
            break;
    }
    m_state = m_detectionActive ? ChannelState::WaitingForCard : ChannelState::Idle;
}

void KeycardChannelUnifiedQtNfc::disconnect()
{
    qDebug() << "KeycardChannelUnifiedQtNfc::disconnect()";
    if (m_target) {
        try {
            m_target->disconnect();  // Disconnect signals - may crash due to Qt bug
        } catch (const std::exception& e) {
            qWarning() << "KeycardChannelUnifiedQtNfc::disconnect() - disconnect() threw exception:" << e.what();
            // Continue cleanup anyway
        } catch (...) {
            qWarning() << "KeycardChannelUnifiedQtNfc::disconnect() - disconnect() threw unknown exception (Qt internal crash)";
            // Continue cleanup anyway
        }
        
        m_target->deleteLater();
        m_target = nullptr;
    }
}

void KeycardChannelUnifiedQtNfc::forceScan()
{
    qDebug() << "KeycardChannelUnifiedQtNfc::forceScan()";
    
    // CRITICAL: Stop detection BEFORE disconnect to avoid emitting signals with invalid state
    // stopDetection() may emit channelStateChanged signal, so do it while target is still valid
    stopDetection();
    
    // Now safe to disconnect - target will be freed
    disconnect();
    
    // Restart detection for new scan
    startDetection();
}

bool KeycardChannelUnifiedQtNfc::isConnected() const
{
    return m_target != nullptr;
}

void KeycardChannelUnifiedQtNfc::onTargetDetected(QNearFieldTarget* target)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::onTargetDetected() called with target:" << target;
    if (!target) {
        return;
    }
    
    QByteArray newUid = target->uid();
    QString newUidHex = newUid.toHex();

    if (m_target != target) {
        disconnect();
    }       
    m_target = target;
    
    // Note: On Android, NFC timeout is extended to 120 seconds by StatusQtActivity
    // when the NFC tag is detected, to support long operations
    
    emit targetDetected(newUidHex);
    updateChannelState(ChannelOperationalState::Reading);
}

bool KeycardChannelUnifiedQtNfc::isTargetStillValid() const
{
    return m_target != nullptr;
}

void KeycardChannelUnifiedQtNfc::onTargetLost(QNearFieldTarget* target)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::onTargetLost() called with target:" << target;
    if (m_target == target) {
        disconnect();
    }
    emit cardRemoved();
}

QString KeycardChannelUnifiedQtNfc::describe(QNearFieldTarget::Error error)
{
    switch(error) {
        case QNearFieldTarget::NoError:
            return "No error has occurred.";
        case QNearFieldTarget::UnknownError:
            return "An unidentified error occurred.";
        case QNearFieldTarget::UnsupportedError:
            return "The requested operation is unsupported by this near field target.";
        case QNearFieldTarget::TargetOutOfRangeError:
            return "The target is no longer within range.";
        case QNearFieldTarget::NoResponseError:
            return "The target did not respond.";
        case QNearFieldTarget::ChecksumMismatchError:
            return "The checksum has detected a corrupted response.";
        case QNearFieldTarget::InvalidParametersError:
            return "Invalid parameters were passed to a tag type specific function.";
        case QNearFieldTarget::ConnectionError:
            return "Failed to connect to the target.";
        case QNearFieldTarget::NdefReadError:
            return "Failed to read NDEF messages from the target.";
        case QNearFieldTarget::NdefWriteError:
            return "Failed to write NDEF messages to the target.";
        case QNearFieldTarget::CommandError:
            return "Failed to send a command to the target.";
        case QNearFieldTarget::TimeoutError:
            return "The request could not be completed within the time specified in waitForRequestCompleted().";
        case QNearFieldTarget::UnsupportedTargetError:
            return "The target used is unsupported. As example this can occur on missing required entitlement and/or privacy settings from the client app.";
    }
}


QByteArray KeycardChannelUnifiedQtNfc::transmit(const QByteArray& apdu)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::transmit() called with apdu:" << apdu.toHex();
    QMutexLocker locker(&m_transmitMutex);
    
    // Update state to Reading when actively transmitting
    updateChannelState(ChannelOperationalState::Reading);
    
    // Check if target is valid
    if (!isTargetStillValid()) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is not valid (null or stale)";
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Target is not valid (null or stale)");
    }
    
    QNearFieldTarget* target = m_target;
    if (!target) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is null";
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Target is null");
    }
    
    QNearFieldTarget::RequestId requestId;
    bool success = false;
    QVariant responseVariant;
    
    // Single try-catch block to protect all Qt NFC operations from race condition crashes
    try {
        // Send command (cross-thread if needed)
        if (QThread::currentThread() == target->thread()) {
            requestId = target->sendCommand(apdu);
        } else {
            bool invokeSuccess = QMetaObject::invokeMethod(this, 
                [target, apdu, &requestId]() {
                    requestId = target->sendCommand(apdu);
                }, Qt::BlockingQueuedConnection);
            
            if (!invokeSuccess) {
                qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - invokeMethod failed";
                updateChannelState(ChannelOperationalState::Error);
                throw std::runtime_error("Failed to invoke sendCommand");
            }
        }
        
        // Wait for completion with 5-second timeout
        // Returns false if tag is lost or timeout occurs
        success = target->waitForRequestCompleted(requestId, 5000);
        
        if (!success) {
            qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - request failed (tag lost or timeout)";
            updateChannelState(ChannelOperationalState::Error);
            // If m_target was nulled, it was tag loss not timeout
            if (!m_target) {
                throw std::runtime_error("Tag lost during transmission");
            }
            throw std::runtime_error("Transmit failed: timeout");
        }
        
        // Get response
        responseVariant = target->requestResponse(requestId);
        if (!responseVariant.isValid()) {
            qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - invalid response from target";
            updateChannelState(ChannelOperationalState::Error);
            throw std::runtime_error("Invalid response from target");
        }
        
    } catch (const std::exception& e) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Qt operation threw exception:" << e.what();
        updateChannelState(ChannelOperationalState::Error);
        // Check if target became stale during the call (tag was lost)
        throw std::runtime_error("Failed to transmit");
    } catch (...) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Qt operation threw unknown exception (Qt targetCheckTimer race crash)";
        updateChannelState(ChannelOperationalState::Error);
        if (!m_target) {
            throw std::runtime_error("Tag lost during transmission (Qt race condition crash)");
        }
        throw std::runtime_error("Transmit failed with unknown error (Qt internal crash)");
    }
    
    QByteArray response = responseVariant.toByteArray();
    qDebug() << "KeycardChannelUnifiedQtNfc::transmit() - success, response:" << response.toHex();
    return response;
}

void KeycardChannelUnifiedQtNfc::updateChannelState(ChannelOperationalState newState)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::updateChannelState() called with state:" << static_cast<int>(newState) << "| current state:" << static_cast<int>(m_channelState);
    if (m_channelState != newState) {
        m_channelState = newState;
        emit channelStateChanged(newState);
    }
}

} // namespace Keycard

