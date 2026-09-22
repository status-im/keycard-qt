// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#include "sim_backend.h"

#include <QDebug>
#include <stdexcept>

namespace {
constexpr int kTimeoutMs = 15000;
constexpr int kProbeTimeoutMs = 500;
}

namespace Keycard {
namespace Test {

SimulatorBackend::SimulatorBackend(const QString& cardId, quint16 port, QObject* parent)
    : KeycardChannelBackend(parent)
    , m_cardId(cardId)
    , m_port(port)
{
}

SimulatorBackend::~SimulatorBackend()
{
    m_socket.close();
}

bool SimulatorBackend::simulatorAvailable(quint16 port)
{
    QTcpSocket probe;
    probe.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (!probe.waitForConnected(kProbeTimeoutMs)) {
        return false;
    }
    probe.write("PING\n");
    if (!probe.waitForBytesWritten(kProbeTimeoutMs) || !probe.waitForReadyRead(kProbeTimeoutMs)) {
        return false;
    }
    return probe.readLine().startsWith("OK");
}

bool SimulatorBackend::ensureSocket()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        return true;
    }
    m_socket.connectToHost(QStringLiteral("127.0.0.1"), m_port);
    return m_socket.waitForConnected(kTimeoutMs);
}

QString SimulatorBackend::request(const QString& line)
{
    if (!ensureSocket()) {
        throw std::runtime_error("keycard-simulator not reachable on 127.0.0.1");
    }

    m_socket.write((line + QStringLiteral("\n")).toUtf8());
    if (!m_socket.waitForBytesWritten(kTimeoutMs)) {
        failRequest(QStringLiteral("write timed out"));
    }

    // One response line per request.
    while (!m_socket.canReadLine()) {
        if (!m_socket.waitForReadyRead(kTimeoutMs)) {
            failRequest(m_socket.error() == QAbstractSocket::SocketTimeoutError
                            ? QStringLiteral("read timed out")
                            : QStringLiteral("closed the connection: %1").arg(m_socket.errorString()));
        }
    }
    return QString::fromUtf8(m_socket.readLine()).trimmed();
}

// Drops the socket before reporting: a failed exchange leaves an unknown amount of the
// reply unread, and reusing it would answer the next request with this one's line.
void SimulatorBackend::failRequest(const QString& why)
{
    m_socket.abort();
    throw std::runtime_error(QStringLiteral("keycard-simulator %1").arg(why).toStdString());
}

bool SimulatorBackend::resetCard()
{
    const QString reply = request(QStringLiteral("RESET %1").arg(m_cardId));
    return reply.startsWith(QStringLiteral("OK"));
}

void SimulatorBackend::startDetection()
{
    m_detecting = true;
    const QString reply = request(QStringLiteral("CREATE %1").arg(m_cardId));
    if (!reply.startsWith(QStringLiteral("OK"))) {
        emit error(QStringLiteral("simulator refused CREATE: %1").arg(reply));
        return;
    }
    m_connected = true;
    // The ATR stands in for the card uid; the applet's own instance uid is
    // randomised per card and read over APDU, not needed here.
    emit targetDetected(reply.mid(3));
    emit channelStateChanged(ChannelOperationalState::Reading);
}

void SimulatorBackend::stopDetection()
{
    m_detecting = false;
    emit targetDetectionStopped(false);
}

void SimulatorBackend::disconnect()
{
    if (m_connected) {
        m_connected = false;
        emit cardRemoved();
    }
}

QByteArray SimulatorBackend::transmit(const QByteArray& apdu)
{
    const QString reply = request(QStringLiteral("APDU %1 %2")
                                     .arg(m_cardId, QString::fromLatin1(apdu.toHex())));
    if (!reply.startsWith(QStringLiteral("OK "))) {
        throw std::runtime_error(QStringLiteral("simulator APDU failed: %1").arg(reply).toStdString());
    }
    const QString hex = reply.mid(3);
    const QByteArray response = QByteArray::fromHex(hex.toLatin1());
    // fromHex() drops invalid characters and yields an empty array for an empty string,
    // so without this a transport-level non-answer reaches the caller as a card error.
    if (response.isEmpty() || response.size() * 2 != hex.size()) {
        throw std::runtime_error(
            QStringLiteral("simulator returned a malformed response: %1").arg(reply).toStdString());
    }
    return response;
}

} // namespace Test
} // namespace Keycard
