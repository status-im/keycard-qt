// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "mock_backend.h"
#include <QDebug>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>

namespace Keycard {
namespace Test {

MockBackend::MockBackend(QObject* parent)
    : KeycardChannelBackend(parent)
    , m_autoConnect(false)
    , m_connected(false)
    , m_detecting(false)
    , m_cardUid("MOCK-CARD-UID-12345678")
    , m_pollingInterval(100)
    , m_logApdu(false)
    , m_defaultResponse(QByteArray::fromHex("9000"))  // Default success
    , m_autoConnectTimer(new QTimer(this))
    , m_transmitDelay(0)
    , m_insertionDelay(0)
    , m_threadSafe(false)
    , m_insertionCount(0)
    , m_removalCount(0)
    , m_errorCount(0)
{
    m_autoConnectTimer->setSingleShot(true);
    connect(m_autoConnectTimer, &QTimer::timeout, this, [this]() {
        if (m_autoConnect && m_detecting && !m_connected) {
            simulateCardInserted();
        }
    });
}

MockBackend::~MockBackend()
{
}

void MockBackend::setAutoConnect(bool autoConnect)
{
    m_autoConnect = autoConnect;
}

void MockBackend::setCardUid(const QString& uid)
{
    m_cardUid = uid;
}

void MockBackend::queueResponse(const QByteArray& response)
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);
    m_responseQueue.enqueue(response);
}

void MockBackend::clearResponses()
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);
    m_responseQueue.clear();
}

void MockBackend::setDefaultResponse(const QByteArray& response)
{
    m_defaultResponse = response;
}

void MockBackend::startDetection()
{
    if (m_detecting) {
        qWarning() << "[MockBackend] Detection already started";
        return;
    }

    m_detecting = true;
    qDebug() << "[MockBackend] Detection started (autoConnect:" << m_autoConnect << ")";

    if (m_autoConnect) {
        // Simulate card detection after short delay
        m_autoConnectTimer->start(50);
    }
}

void MockBackend::stopDetection()
{
    if (!m_detecting) {
        return;
    }

    m_detecting = false;
    m_autoConnectTimer->stop();
    qDebug() << "[MockBackend] Detection stopped";
}

void MockBackend::disconnect()
{
    if (m_connected) {
        simulateCardRemoved();
    }
}

bool MockBackend::isConnected() const
{
    // Only auto-reconnect if we're waiting for a card (for testing)
    // This ensures waitForCard() doesn't hang, but allows tests to check disconnected state
    if (!m_connected && m_state == ChannelState::WaitingForCard) {
        qDebug() << "[MockBackend] Auto-reconnecting card (waiting for card, mock should always be available)";
        // Use const_cast to allow modification (this is safe in tests)
        const_cast<MockBackend*>(this)->simulateCardInserted();
    }
    return m_connected;
}

QByteArray MockBackend::transmit(const QByteArray& apdu)
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);

    // Simulate transmit delay for testing timeouts
    if (m_transmitDelay > 0) {
        QThread::msleep(m_transmitDelay);
    }

    // Check for error simulation
    if (!m_nextThrowMessage.isEmpty()) {
        QString msg = m_nextThrowMessage;
        m_nextThrowMessage.clear();
        throw std::runtime_error(msg.toStdString());
    }

    // Check connection
    if (!m_connected) {
        throw std::runtime_error("Not connected to card");
    }

    // Log if enabled
    if (m_logApdu) {
        qDebug() << "[MockBackend] TX:" << apdu.toHex();
    }

    // Track transmitted APDU
    m_transmittedApdus.append(apdu);

    // Get response from queue or use default
    QByteArray response;
    if (!m_responseQueue.isEmpty()) {
        response = m_responseQueue.dequeue();
    } else {
        response = m_defaultResponse;
    }

    if (m_logApdu) {
        qDebug() << "[MockBackend] RX:" << response.toHex();
    }

    return response;
}

void MockBackend::simulateCardInserted()
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);

    if (m_connected) {
        qWarning() << "[MockBackend] Card already inserted";
        return;
    }

    // Simulate insertion delay for testing race conditions
    if (m_insertionDelay > 0) {
        locker.unlock();  // Release lock during delay
        QThread::msleep(m_insertionDelay);
        locker.relock();
    }

    m_connected = true;
    m_insertionCount++;
    qDebug() << "[MockBackend] Card inserted, UID:" << m_cardUid;
    emit targetDetected(m_cardUid);
}

void MockBackend::simulateCardRemoved()
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);

    if (!m_connected) {
        qWarning() << "[MockBackend] No card to remove";
        return;
    }

    m_connected = false;
    m_removalCount++;
    qDebug() << "[MockBackend] Card removed";
    emit cardRemoved();
}

void MockBackend::simulateError(const QString& errorMessage)
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);

    m_errorCount++;
    qDebug() << "[MockBackend] Simulating error:" << errorMessage;
    emit error(errorMessage);
}

void MockBackend::setNextTransmitThrows(const QString& errorMessage)
{
    m_nextThrowMessage = errorMessage;
}

void MockBackend::setState(ChannelState state)
{
    m_state = state;
    
    // Auto-reconnect when waiting for card (for testing)
    // This ensures waitForCard() doesn't hang in tests - the mock should always
    // have a card available when waiting
    if (state == ChannelState::WaitingForCard && !m_connected) {
        qDebug() << "[MockBackend] State set to WaitingForCard, auto-reconnecting card";
        // Reconnect immediately so waitForCard() can return
        simulateCardInserted();
    }
}

void MockBackend::forceScan()
{
    qDebug() << "[MockBackend] Force scan requested";
    // If detection is active and we have auto-connect enabled, trigger card detection
    if (m_detecting && m_autoConnect && !m_connected) {
        m_autoConnectTimer->stop();
        simulateCardInserted();
    } else if (m_detecting && !m_connected) {
        // Even without auto-connect, if we're detecting, simulate a card insertion
        // This helps tests that need to trigger a scan
        simulateCardInserted();
    }
}

void MockBackend::reset()
{
    // Thread-safe lock if enabled
    QMutexLocker locker(m_threadSafe ? &m_mutex : nullptr);

    qDebug() << "[MockBackend] Resetting state";

    // Stop detection
    if (m_detecting) {
        stopDetection();
    }

    // For testing: only reconnect if we were waiting for a card
    // This ensures waitForCard() doesn't hang, but allows tests to check disconnected state
    bool wasWaiting = (m_state == ChannelState::WaitingForCard);
    
    if (m_connected) {
        m_connected = false;
        emit cardRemoved();
    }
    
    // Auto-reconnect only if we were waiting for a card (for testing)
    // This ensures waitForCard() doesn't hang, but allows tests to verify disconnected state
    if (wasWaiting) {
        qDebug() << "[MockBackend] Auto-reconnecting after reset (was waiting for card)";
        simulateCardInserted();
    }

    // Clear queues and tracking
    m_responseQueue.clear();
    m_transmittedApdus.clear();
    m_nextThrowMessage.clear();

    // Reset statistics
    m_insertionCount = 0;
    m_removalCount = 0;
    m_errorCount = 0;

    // Reset to defaults (but keep autoConnect and threading settings if they were set)
    // m_autoConnect = false;  // Don't reset autoConnect
    // m_threadSafe = false;   // Don't reset threading mode
    m_cardUid = "MOCK-CARD-UID-12345678";
    m_defaultResponse = QByteArray::fromHex("9000");
    m_transmitDelay = 0;
    m_insertionDelay = 0;
}

} // namespace Test
} // namespace Keycard

