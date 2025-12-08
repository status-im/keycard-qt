// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/backends/keycard_channel_backend.h"
#include <QDebug>

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    #include "keycard-qt/backends/keycard_channel_unified_qt_nfc.h"
#else
    #include "keycard-qt/backends/keycard_channel_pcsc.h"
#endif

namespace Keycard {

// Default constructor - creates platform-specific backend
KeycardChannel::KeycardChannel(QObject* parent)
    : QObject(parent)
    , m_backend(nullptr)
    , m_ownsBackend(true)
{
    qDebug() << "========================================";
    qDebug() << "KeycardChannel: Initializing with default platform backend";
    
    m_backend = createDefaultBackend();
    
    if (!m_backend) {
        qCritical() << "KeycardChannel: Failed to create backend!";
        return;
    }
    
    qDebug() << "KeycardChannel: Backend:" << m_backend->backendName();
    qDebug() << "========================================";
    
    // Connect backend signals to our signals (pass-through)
    connect(m_backend, &KeycardChannelBackend::readerAvailabilityChanged,
            this, &KeycardChannel::readerAvailabilityChanged);
    
    connect(m_backend, &KeycardChannelBackend::targetDetected,
            this, [this](const QString& uid) {
        m_targetUid = uid;
        emit targetDetected(uid);
    });
    
    connect(m_backend, &KeycardChannelBackend::cardRemoved,
            this, [this]() {
        m_targetUid.clear();
        emit targetLost();
    });
    
    connect(m_backend, &KeycardChannelBackend::error,
            this, &KeycardChannel::error);
    
    connect(m_backend, &KeycardChannelBackend::channelStateChanged,
            this, &KeycardChannel::channelStateChanged);
}

// DI constructor - accepts injected backend
KeycardChannel::KeycardChannel(KeycardChannelBackend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_ownsBackend(false)  // Don't delete injected backend
{
    qDebug() << "========================================";
    qDebug() << "KeycardChannel: Initializing with injected backend";
    
    if (!m_backend) {
        qCritical() << "KeycardChannel: Injected backend is null!";
        return;
    }
    
    // Set parent to manage lifetime if backend has no parent
    if (!m_backend->parent()) {
        m_backend->setParent(this);
        m_ownsBackend = true;  // We'll manage it now
    }
    
    qDebug() << "KeycardChannel: Backend:" << m_backend->backendName();
    qDebug() << "========================================";
    
    // Connect backend signals to our signals (pass-through)
    connect(m_backend, &KeycardChannelBackend::readerAvailabilityChanged,
            this, &KeycardChannel::readerAvailabilityChanged);
    
    connect(m_backend, &KeycardChannelBackend::targetDetected,
            this, [this](const QString& uid) {
        m_targetUid = uid;
        emit targetDetected(uid);
    });
    
    connect(m_backend, &KeycardChannelBackend::cardRemoved,
            this, [this]() {
        m_targetUid.clear();
        emit targetLost();
    });
    
    connect(m_backend, &KeycardChannelBackend::error,
            this, &KeycardChannel::error);
    
    connect(m_backend, &KeycardChannelBackend::channelStateChanged,
            this, &KeycardChannel::channelStateChanged);
}

KeycardChannelBackend* KeycardChannel::createDefaultBackend()
{
    // Factory: Create appropriate backend based on platform
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    qDebug() << "KeycardChannel: Creating unified Qt NFC backend (All platforms)";
    return new KeycardChannelUnifiedQtNfc(this);
#else
    qDebug() << "KeycardChannel: Creating PC/SC backend (Desktop)";
    return new KeycardChannelPcsc(this);
#endif
}

KeycardChannel::~KeycardChannel()
{
    qDebug() << "KeycardChannel: Destructor";
    // m_backend will be deleted automatically (QObject parent-child)
}

void KeycardChannel::startDetection()
{
    if (m_backend) {
        m_backend->startDetection();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
        emit error("No backend available");
    }
}

void KeycardChannel::stopDetection()
{
    if (m_backend) {
        m_backend->stopDetection();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
    }
}

void KeycardChannel::forceScan()
{
    if (m_backend) {
        // Try to call forceScan() on the backend if it supports it
        m_backend->forceScan();
    }
}

void KeycardChannel::disconnect()
{
    if (m_backend) {
        m_backend->disconnect();
    } else {
        qWarning() << "KeycardChannel: No backend available!";
    }
}

QString KeycardChannel::targetUid() const
{
    return m_targetUid;
}

QString KeycardChannel::backendName() const
{
    if (m_backend) {
        return m_backend->backendName();
    }
    return "None";
}

bool KeycardChannel::requestCardAtStartup()
{
    if (!m_backend) {
        qWarning() << "KeycardChannel: No backend available for requestCardAtStartup";
        return false;
    }
    
    // iOS/Android: Use runtime check to access platform-specific method
#if defined(KEYCARD_BACKEND_QT_NFC)
    // Try to cast to KeycardChannelQtNfc to access iOS/Android-specific method
    auto* qtNfcBackend = dynamic_cast<KeycardChannelQtNfc*>(m_backend);
    if (qtNfcBackend) {
        qDebug() << "KeycardChannel: Calling Qt NFC backend requestCardAtStartup()";
        return qtNfcBackend->requestCardAtStartup();
    }
#endif
    
    // For PC/SC backend, this is a no-op - card detection runs in background
    qDebug() << "KeycardChannel: PC/SC backend - no startup initialization required";
    return true;
}

void KeycardChannel::setState(ChannelState state)
{
    if (m_backend) {
        m_backend->setState(state);
    }
}

ChannelState KeycardChannel::state() const
{
    if (m_backend) {
        return m_backend->state();
    }
    return ChannelState::Idle;
}

ChannelOperationalState KeycardChannel::channelState() const
{
    if (m_backend) {
        return m_backend->channelState();
    }
    return ChannelOperationalState::Idle;
}

QByteArray KeycardChannel::transmit(const QByteArray& apdu)
{
    if (!m_backend) {
        throw std::runtime_error("No backend available");
    }
    return m_backend->transmit(apdu);
}

bool KeycardChannel::isConnected() const
{
    if (m_backend) {
        return m_backend->isConnected();
    }
    return false;
}

} // namespace Keycard
