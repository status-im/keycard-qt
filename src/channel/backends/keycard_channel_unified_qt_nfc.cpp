// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/backends/keycard_channel_unified_qt_nfc.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QTimer>
#endif

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
    connect(this, &KeycardChannelUnifiedQtNfc::channelStateChanged, this, [this](ChannelOperationalState state) {
        if (state == ChannelOperationalState::WaitingForKeycard) {
            m_manager->setUserInformation("Waiting for keycard. Please hold the keycard near the device.");
        } else if (state == ChannelOperationalState::Reading) {
            m_manager->setUserInformation("Reading keycard. Please hold the keycard near the device.");
        } else if (state == ChannelOperationalState::Error) {
            m_manager->setUserInformation("Error reading keycard. Please try again.");
        }
    });
}

KeycardChannelUnifiedQtNfc::~KeycardChannelUnifiedQtNfc()
{
    stopDetection();
    disconnect();
}

void KeycardChannelUnifiedQtNfc::startDetection()
{
    qDebug() << "KeycardChannelUnifiedQtNfc::startDetection()";

#ifdef Q_OS_ANDROID
    // Android-only: emitted when the OS NFC adapter is toggled on/off (or transitioning).
    QObject::connect(m_manager, &QNearFieldManager::adapterStateChanged,
            this, &KeycardChannelUnifiedQtNfc::onAdapterStateChanged, Qt::UniqueConnection);
    // Qt 6.9 Android NFC uses foreground dispatch and may fail if started before the Activity
    // is fully active/resumed. If we call too early, Qt may consider discovery enabled while
    // the platform never actually starts delivering tag intents until a background→foreground cycle.
    //
    // Mitigation: defer startTargetDetection() until the Qt app is ApplicationActive.
    auto *app = QCoreApplication::instance();
    auto *guiApp = qobject_cast<QGuiApplication *>(app);
    qDebug() << "KeycardChannelUnifiedQtNfc:Verifying app state=" << static_cast<int>(guiApp->applicationState());

    if (guiApp && guiApp->applicationState() != Qt::ApplicationActive) {
        qDebug() << "KeycardChannelUnifiedQtNfc: App not active yet, deferring NFC start. state="
                 << static_cast<int>(guiApp->applicationState());

        if (!m_waitingForAppActive) {
            m_waitingForAppActive = true;
            m_appStateConn = connect(guiApp, &QGuiApplication::applicationStateChanged,
                                     this, [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    return;
                }

                // One-shot: once active, drop the guard and retry immediately on the event loop.
                m_waitingForAppActive = false;
                QObject::disconnect(m_appStateConn);
                m_appStateConn = {};

                QTimer::singleShot(0, this, [this]() {
                    this->startDetection();
                });
            });
        }

        // Keep UX consistent: we are logically waiting for a keycard, just not starting NFC yet.
        emitChannelState(ChannelOperationalState::WaitingForKeycard);
        return;
    }
#endif

    if (!m_manager->isSupported(QNearFieldTarget::TagTypeSpecificAccess)) {
        emitChannelState(ChannelOperationalState::NotSupported);
        emit readerAvailabilityChanged(false);
        emit error("NFC not supported on this platform");
        m_detectionActive = false;
        return;
    }

    if (!m_manager->isEnabled()) {
        emitChannelState(ChannelOperationalState::NotAvailable);
        emit readerAvailabilityChanged(false);
        emit error("NFC/PCSC is disabled");
        m_detectionActive = false;
        return;
    }

    m_manager->startTargetDetection(QNearFieldTarget::TagTypeSpecificAccess);
    m_detectionActive = true;
    emit readerAvailabilityChanged(true);
    emitChannelState(ChannelOperationalState::WaitingForKeycard);
}

void KeycardChannelUnifiedQtNfc::stopDetection()
{
#ifdef Q_OS_ANDROID
    QObject::disconnect(m_manager, &QNearFieldManager::adapterStateChanged,
        this, &KeycardChannelUnifiedQtNfc::onAdapterStateChanged);
#endif
    // iOS NFC session management can be sensitive to threading; serialize with transmit.
#ifdef Q_OS_IOS
    QMutexLocker locker(&m_transmitMutex);
    qDebug() << "KeycardChannelUnifiedQtNfc::stopDetection()";
    if (m_detectionActive) {
        m_manager->stopTargetDetection();
        m_detectionActive = false;
    }
#endif
    emitChannelState(ChannelOperationalState::Idle);
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
    emitChannelState(ChannelOperationalState::Reading);
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

#ifdef Q_OS_ANDROID
void KeycardChannelUnifiedQtNfc::onAdapterStateChanged(QNearFieldManager::AdapterState state)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::onAdapterStateChanged() state=" << static_cast<int>(state);

    switch (state) {
        case QNearFieldManager::AdapterState::Offline:
        case QNearFieldManager::AdapterState::TurningOff: {
            m_manager->stopTargetDetection();
            m_detectionActive = false;
            disconnect();
            // NFC is going away: stop scanning and drop any current target.
            // Do NOT emit Idle in-between (avoid flicker); go straight to NotAvailable.
            emit readerAvailabilityChanged(false);
            emitChannelState(ChannelOperationalState::NotAvailable);
            break;
        }

        case QNearFieldManager::AdapterState::Online: {
            emit readerAvailabilityChanged(true);

            // If higher-level logic expects us to be scanning, (re)start now.
            // Use event-loop deferral to avoid re-entrancy if this signal fires during Qt NFC internals.
            QTimer::singleShot(0, this, [this]() { this->setState(ChannelState::WaitingForCard); });
            break;
        }

        case QNearFieldManager::AdapterState::TurningOn: {
            // Transitional state: avoid emitting errors; keep UX in "waiting" if applicable.
            if (m_state == ChannelState::WaitingForCard) {
                emitChannelState(ChannelOperationalState::WaitingForKeycard);
            }
            break;
        }
    }
}
#endif

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
    emitChannelState(ChannelOperationalState::Reading);
    
    // Check if target is valid
    if (!isTargetStillValid()) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is not valid (null or stale)";
        emitChannelState(ChannelOperationalState::Error);
        throw std::runtime_error("Target is not valid (null or stale)");
    }
    
    QNearFieldTarget* target = m_target;
    if (!target) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - target is null";
        emitChannelState(ChannelOperationalState::Error);
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
                emitChannelState(ChannelOperationalState::Error);
                throw std::runtime_error("Failed to invoke sendCommand");
            }
        }
        
        // Wait for completion with 5-second timeout
        // Returns false if tag is lost or timeout occurs
        success = target->waitForRequestCompleted(requestId, 5000);
        
        if (!success) {
            qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - request failed (tag lost or timeout)";
            emitChannelState(ChannelOperationalState::Error);
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
            emitChannelState(ChannelOperationalState::Error);
            throw std::runtime_error("Invalid response from target");
        }
        
    } catch (const std::exception& e) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Qt operation threw exception:" << e.what();
        emitChannelState(ChannelOperationalState::Error);
        // Check if target became stale during the call (tag was lost)
        throw std::runtime_error("Failed to transmit");
    } catch (...) {
        qWarning() << "KeycardChannelUnifiedQtNfc::transmit() - Qt operation threw unknown exception (Qt targetCheckTimer race crash)";
        emitChannelState(ChannelOperationalState::Error);
        if (!m_target) {
            throw std::runtime_error("Tag lost during transmission (Qt race condition crash)");
        }
        throw std::runtime_error("Transmit failed with unknown error (Qt internal crash)");
    }
    
    QByteArray response = responseVariant.toByteArray();
    qDebug() << "KeycardChannelUnifiedQtNfc::transmit() - success, response:" << response.toHex();
    return response;
}

void KeycardChannelUnifiedQtNfc::emitChannelState(ChannelOperationalState newState)
{
    qDebug() << "KeycardChannelUnifiedQtNfc::emitChannelState() called with state:" << static_cast<int>(newState);
    emit channelStateChanged(newState);
}

} // namespace Keycard

