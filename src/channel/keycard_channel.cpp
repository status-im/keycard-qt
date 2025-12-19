// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/backends/keycard_channel_backend.h"
#include "keycard-qt/globalplatform/gp_constants.h"
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
    
    QByteArray response = m_backend->transmit(apdu);
    
    // Handle incomplete response (T=0 protocol, ISO 7816-4)
    // SW1 = 0x61 means "response bytes still available", SW2 = bytes remaining
    // This is transparent to the caller - we automatically fetch remaining data
    if (response.size() >= 2) {
        uint8_t sw1 = static_cast<uint8_t>(response[response.size() - 2]);
        uint8_t sw2 = static_cast<uint8_t>(response[response.size() - 1]);
        
        // Check if this is an incomplete response AND we're not already doing GET RESPONSE
        if (sw1 == GlobalPlatform::SW1_RESPONSE_DATA_INCOMPLETE && apdu.size() >= 2) {
            uint8_t cla = static_cast<uint8_t>(apdu[0]);
            uint8_t ins = static_cast<uint8_t>(apdu[1]);
            
            // Avoid infinite loop - don't GET RESPONSE on a GET RESPONSE
            if (cla != GlobalPlatform::CLA_ISO7816 || ins != GlobalPlatform::INS_GET_RESPONSE) {
                qDebug() << "KeycardChannel::transmit(): More data available (SW1=0x61, SW2=" 
                         << QString("0x%1").arg(sw2, 2, 16, QChar('0'))
                         << "), sending GET RESPONSE";
                
                // Build GET RESPONSE command: [CLA=0x00, INS=0xC0, P1=0x00, P2=0x00, Le=SW2]
                QByteArray getResponse;
                getResponse.append(static_cast<char>(GlobalPlatform::CLA_ISO7816));    // CLA
                getResponse.append(static_cast<char>(GlobalPlatform::INS_GET_RESPONSE)); // INS
                getResponse.append(static_cast<char>(0x00));                            // P1
                getResponse.append(static_cast<char>(0x00));                            // P2
                getResponse.append(static_cast<char>(sw2));                             // Le (expected length)
                
                // Recursively call transmit to get remaining data
                // This handles chained responses (multiple 0x61 status codes) automatically
                return transmit(getResponse);
            }
        }
    }
    
    return response;
}

bool KeycardChannel::isConnected() const
{
    if (m_backend) {
        return m_backend->isConnected();
    }
    return false;
}

} // namespace Keycard
