// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_unified_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>

namespace Keycard {

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
    
    // Clean up pending requests
    for (PendingRequest* req : m_pendingRequests) {
        delete req;
    }
    m_pendingRequests.clear();
}

void KeycardChannelUnifiedQtNfc::startDetection()
{
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
    qDebug() << "KeycardChannelUnifiedQtNfc::stopDetection()";
    if (m_detectionActive) {
        m_manager->stopTargetDetection();
        m_detectionActive = false;
    }
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
    if (m_target) {
        m_target->disconnect();
        m_target->deleteLater();
        m_target = nullptr;
    }
}

void KeycardChannelUnifiedQtNfc::forceScan()
{
    qDebug() << "KeycardChannelUnifiedQtNfc::forceScan()";
    disconnect();
    stopDetection();
    startDetection();
}

bool KeycardChannelUnifiedQtNfc::isConnected() const
{
    return m_detectionActive && m_target != nullptr;
}

void KeycardChannelUnifiedQtNfc::onTargetDetected(QNearFieldTarget* target)
{
    if (!target) {
        return;
    }
    
    QByteArray newUid = target->uid();
    QString newUidHex = newUid.toHex();

    if (m_target != target) {
        disconnect();
    }
        
    m_target = target;
    setupTargetSignals(target);
    emit targetDetected(newUidHex);
    updateChannelState(ChannelOperationalState::Reading);
}

void KeycardChannelUnifiedQtNfc::onTargetLost(QNearFieldTarget* target)
{
    if (m_target != target) {
        if (target) {
            target->deleteLater();
        }
        return;
    }
    disconnect();
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

void KeycardChannelUnifiedQtNfc::setupTargetSignals(QNearFieldTarget* target)
{
    connect(target, &QNearFieldTarget::requestCompleted,
            this, [this](const QNearFieldTarget::RequestId& id) {
        if (!m_target) {
            return;
        }
        
        // Find pending request by ID
        for (PendingRequest* req : m_pendingRequests) {
            if (req->requestId == id) {
                QVariant result = m_target->requestResponse(id);
                req->response = result.toByteArray();
                req->completed = true;
                if (req->eventLoop) {
                    req->eventLoop->quit();
                }
                break;
            }
        }
    }, Qt::DirectConnection);
    
    connect(target, &QNearFieldTarget::error,
            this, [this](QNearFieldTarget::Error error, const QNearFieldTarget::RequestId& id) {

        qWarning() << "QNearFieldTarget Error: " << describe(error);

        // Find pending request by ID
        for (PendingRequest* req : m_pendingRequests) {
            if (req->requestId == id) {
                req->completed = false;
                if (req->eventLoop) {
                    req->eventLoop->quit();
                }
                break;
            }
        }
        
    }, Qt::DirectConnection);
}

QByteArray KeycardChannelUnifiedQtNfc::transmit(const QByteArray& apdu)
{
    QMutexLocker locker(&m_transmitMutex);
    
    // Update state to Reading when actively transmitting
    updateChannelState(ChannelOperationalState::Reading);
    
    if (!m_target) {
        // Not connected
        QByteArray error;
        error.append(static_cast<char>(0x6A)); // SW1
        error.append(static_cast<char>(0x89)); // SW2: Not connected
        return error;
    }

    // Send command (marshal to main thread if needed)
    QNearFieldTarget::RequestId requestId;
    QNearFieldTarget* target = m_target; // Capture locally
    
    if (QThread::currentThread() == target->thread()) {
        requestId = target->sendCommand(apdu);
    } else {
        QMetaObject::invokeMethod(this, [target, apdu, &requestId]() {
            requestId = target->sendCommand(apdu);
        }, Qt::BlockingQueuedConnection);
    }
    
    // If sendCommand failed, the tag might be stale (Android)
    if (!requestId.isValid()) {
        qWarning() << "KeycardChannelUnifiedQtNfc: sendCommand failed - tag may be stale";
        QByteArray error;
        error.append(static_cast<char>(0x6F)); // SW1
        error.append(static_cast<char>(0x03)); // SW2: Invalid request ID
        updateChannelState(ChannelOperationalState::Error);
        return error;
    }
    
    // Wait for response
    QEventLoop eventLoop;
    PendingRequest* pending = new PendingRequest;
    pending->requestId = requestId;
    pending->eventLoop = &eventLoop;
    pending->completed = false;
    m_pendingRequests.append(pending);
    
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000); // 5s timeout
    connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    timeout.start();
    
    eventLoop.exec();
    timeout.stop();
    
    // Find and remove the pending request
    PendingRequest* foundReq = nullptr;
    for (int i = 0; i < m_pendingRequests.size(); ++i) {
        if (m_pendingRequests[i]->requestId == requestId) {
            foundReq = m_pendingRequests[i];
            m_pendingRequests.removeAt(i);
            break;
        }
    }
    
    if (!foundReq || !foundReq->completed) {
        if (foundReq) {
            delete foundReq;
        }
        
        updateChannelState(ChannelOperationalState::Error);
        QByteArray error;
        error.append(static_cast<char>(0x6F)); // SW1
        error.append(static_cast<char>(0x00)); // SW2: Timeout or stale tag
        return error;
    }
    
    QByteArray response = foundReq->response;
    delete foundReq;
    
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

