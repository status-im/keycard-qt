// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_unified_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#endif

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
    
    // Clean up pending requests
    for (PendingRequest* req : m_pendingRequests) {
        delete req;
    }
    m_pendingRequests.clear();
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
        m_target->disconnect();  // Disconnect signals
        m_target->deleteLater();
        m_targetIsStale = true;  // Mark as stale before cleanup
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
    m_targetIsStale = false;  // Fresh target
    
    // Note: On Android, NFC timeout is extended to 10 seconds by StatusQtActivity
    // when the NFC tag is detected, to support long GlobalPlatform operations
    
    setupTargetSignals(target);
    emit targetDetected(newUidHex);
    updateChannelState(ChannelOperationalState::Reading);
}

bool KeycardChannelUnifiedQtNfc::isTargetStillValid() const
{
    if (!m_target) {
        return false;
    }
    
    if (m_targetIsStale) {
        return false;
    }
    
#ifdef Q_OS_ANDROID
    // On Android, check if there are any pending JNI exceptions
    // This can happen if Qt tried to access a stale tag
    QJniEnvironment env;
    if (env->ExceptionCheck()) {
        qWarning() << "JNI exception detected - tag is likely stale";
        env->ExceptionDescribe();
        env->ExceptionClear();
        return false;
    }
#endif
    
    return true;
}

void KeycardChannelUnifiedQtNfc::onTargetLost(QNearFieldTarget* target)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::onTargetLost() called with target:" << target;
    if (m_target == target) {
        disconnect();
    }
    
    // CRITICAL: Mark all pending requests as failed immediately
    // The tag is gone, so any waiting transmit() calls should abort
    for (PendingRequest* req : m_pendingRequests) {
        if (req->eventLoop && !req->completed) {
            req->completed = false;  // Mark as failed
            req->eventLoop->quit();  // Wake up waiting thread
        }
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

void KeycardChannelUnifiedQtNfc::setupTargetSignals(QNearFieldTarget* target)
{
    connect(target, &QNearFieldTarget::requestCompleted,
            this, [this](const QNearFieldTarget::RequestId& id) {
        qDebug() << "KeycardChannelUnifiedQtNfc::requestCompleted() called with id:" << id.isValid();
        
        // CRITICAL: Check if target is still valid before accessing it
        if (!isTargetStillValid()) {
            qWarning() << "KeycardChannelUnifiedQtNfc::requestCompleted() called with invalid/stale target";
            // Mark all pending requests as failed
            for (PendingRequest* req : m_pendingRequests) {
                if (req->requestId == id && req->eventLoop && !req->completed) {
                    req->completed = false;
                    req->eventLoop->quit();
                }
            }
            return;
        }
        qDebug() << "KeycardChannelUnifiedQtNfc::requestCompleted() called with target:" << m_target;
        
        // Find pending request by ID
        for (PendingRequest* req : m_pendingRequests) {
            if (req->requestId == id) {
                // CRITICAL: Wrap requestResponse in try-catch
                // Qt might crash here if Android tag is stale
                try {
                    QVariant result = m_target->requestResponse(id);
                    req->response = result.toByteArray();
                    req->completed = true;
                    
#ifdef Q_OS_ANDROID
                    // Check if requestResponse triggered JNI exceptions
                    QJniEnvironment env;
                    if (env->ExceptionCheck()) {
                        qWarning() << "JNI exception during requestResponse - tag became stale";
                        env->ExceptionDescribe();
                        env->ExceptionClear();
                        m_targetIsStale = true;
                        req->completed = false;
                    }
#endif
                } catch (const std::exception& e) {
                    qWarning() << "Exception getting request response:" << e.what();
                    req->completed = false;
                    m_targetIsStale = true;
                } catch (...) {
                    qWarning() << "Unknown exception getting request response";
                    req->completed = false;
                    m_targetIsStale = true;
                }
                
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

    qDebug() << "KeycardChannelUnifiedQtNfc::transmit() called with apdu:" << apdu.toHex();
    
    // Update state to Reading when actively transmitting
    updateChannelState(ChannelOperationalState::Reading);
    
    // CRITICAL: Check if target is truly valid (not just non-null)
    if (!isTargetStillValid()) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is not valid (null or stale)";
        updateChannelState(ChannelOperationalState::Error);
        
        // Not connected or tag is stale
        throw std::runtime_error("Target is not valid (null or stale)");
    }

    // Create event loop and pending request structure
    QEventLoop eventLoop;
    PendingRequest* pending = new PendingRequest;
    pending->eventLoop = &eventLoop;
    pending->completed = false;
    
    // Send command (marshal to main thread if needed)
    // CRITICAL: Must register pending request IMMEDIATELY after getting requestId
    // to prevent race where response arrives before we start waiting
    QNearFieldTarget::RequestId requestId;
    QNearFieldTarget* target = m_target; // Capture locally
    
    // CRITICAL: Check if target is valid before using it
    if (!target) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is null, cannot send";
        delete pending;
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Target is null, cannot send");
    }
    
    // Wrap sendCommand in try-catch to handle Qt's internal Android JNI exceptions
    // If the Android tag is removed, Qt's sendCommand() will throw or crash
    try {
        if (QThread::currentThread() == target->thread()) {
            // Same thread: send and register immediately (no gap for race condition)
            requestId = target->sendCommand(apdu);
            pending->requestId = requestId;
            m_pendingRequests.append(pending);
        } else {
            // Cross-thread: send command and get ID synchronously
            bool invokeSuccess = QMetaObject::invokeMethod(this, [target, apdu, &requestId]() {
                requestId = target->sendCommand(apdu);
            }, Qt::BlockingQueuedConnection);
            
            if (!invokeSuccess) {
                qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - invokeMethod failed";
                delete pending;
                updateChannelState(ChannelOperationalState::Error);
                throw std::runtime_error("Failed to invoke sendCommand");
            }
            
            // Register immediately after getting ID (still in transmit() call)
            pending->requestId = requestId;
            m_pendingRequests.append(pending);
        }
    } catch (const std::exception& e) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Exception during sendCommand:" << e.what();
        delete pending;
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Exception during sendCommand - tag may have been removed");
    } catch (...) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Unknown exception during sendCommand";
        delete pending;
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Unknown exception during sendCommand - tag may have been removed");
    }

    // QThread::sleep(1);
    
#ifdef Q_OS_ANDROID
    // Check if sendCommand triggered any JNI exceptions (tag was removed during call)
    QJniEnvironment env;
    if (env->ExceptionCheck()) {
        qWarning() << "KeycardChannelUnifiedQtNfc: JNI exception after sendCommand - tag was removed";
        env->ExceptionDescribe();
        env->ExceptionClear();
        
        m_targetIsStale = true;
        m_pendingRequests.removeOne(pending);
        delete pending;
        
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Tag was removed during sendCommand");
    }
#endif
    
    // If sendCommand failed, the tag might be stale (Android)
    if (!requestId.isValid()) {
        qWarning() << "KeycardChannelUnifiedQtNfc: sendCommand failed - tag may be stale";
        
        m_targetIsStale = true;  // Mark as stale
        
        // Remove the pending request we just added
        m_pendingRequests.removeOne(pending);
        delete pending;
        
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Send command failed, tag may be stale");
    }
    
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(120000); // 120s timeout
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
        qWarning() << "KeycardChannelUnifiedQtNfc: Timeout or stale tag detected " << (foundReq ? "found" : "not found") << "| completed:" << (foundReq ? foundReq->completed : false);
        if (foundReq) {
            delete foundReq;
        }
        
        updateChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Timeout or stale tag detected");
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

