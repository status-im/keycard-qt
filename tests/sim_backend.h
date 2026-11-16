// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "keycard-qt/backends/keycard_channel_backend.h"

#include <QByteArray>
#include <QString>
#include <QTcpSocket>

namespace Keycard {
namespace Test {

/**
 * @brief Backend that drives the real Keycard applet in the keycard-simulator.
 *
 * The simulator (status-keycard-qt/test/keycard-simulator) runs the applet in
 * jcardsim behind a line protocol over TCP: CREATE/RESET/ATR/APDU/PING. Unlike
 * MockBackend, responses come from the applet, so PIN and PUK counters, the
 * secure channel and pairing all behave as they do on a physical card.
 *
 * Each card id addresses a separate card and its state survives until reset, so
 * a test calls resetCard() to start from a blank, post-FactoryReset card. The
 * simulator's applet state is not safe for concurrent use across ids, so the
 * tests that use this hold a ctest resource lock rather than running in parallel.
 *
 * The socket takes no lock, so drive this from the thread that constructed it —
 * it is not safe behind CommunicationManager, which moves itself to its own thread.
 */
class SimulatorBackend : public KeycardChannelBackend
{
    Q_OBJECT

public:
    explicit SimulatorBackend(const QString& cardId = QStringLiteral("A"),
                              quint16 port = 9025,
                              QObject* parent = nullptr);
    ~SimulatorBackend() override;

    static bool simulatorAvailable(quint16 port = 9025);

    /// Recreate the card blank. Returns false if the simulator refused.
    bool resetCard();

    // KeycardChannelBackend interface
    void startDetection() override;
    void stopDetection() override;
    bool isDetectionActive() const override { return m_detecting; }
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QByteArray transmit(const QByteArray& apdu) override;
    QString backendName() const override { return QStringLiteral("Keycard Simulator"); }
    void setState(ChannelState state) override { m_state = state; }
    ChannelState state() const override { return m_state; }
    void forceScan() override {}

private:
    bool ensureSocket();
    QString request(const QString& line);
    [[noreturn]] void failRequest(const QString& why);

    QString m_cardId;
    quint16 m_port;
    QTcpSocket m_socket;
    bool m_detecting = false;
    bool m_connected = false;
    ChannelState m_state = ChannelState::Idle;
};

} // namespace Test
} // namespace Keycard
