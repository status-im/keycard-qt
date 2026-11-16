// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#include "sim_backend.h"

#include "keycard-qt/command_set.h"
#include "keycard-qt/keycard_channel.h"

#include <QString>
#include <memory>

namespace Keycard {
namespace Test {

/// The applet derives the alt (duress) PIN from the first six digits of the PUK
/// when INIT carries no explicit one, and verifyPIN() accepts either slot. The
/// PUK below therefore must not start with the PIN, or every test here would
/// exercise the duress path instead of the main one.
inline const QString kSimPin = QStringLiteral("123456");
inline const QString kSimPuk = QStringLiteral("987654321012");
inline const QString kSimWrongPin = QStringLiteral("654321");
inline const QString kSimPairing = QStringLiteral("KeycardDefaultPairing");

/**
 * @brief A simulator card and the CommandSet driving it.
 *
 * failure() carries the reason when a step returns false, so a test can report the
 * applet's own error rather than a bare boolean.
 */
class SimCard
{
public:
    explicit SimCard(const QString& cardId)
        : m_backend(new SimulatorBackend(cardId))
        , m_channel(std::make_shared<KeycardChannel>(m_backend))
        , m_cmdSet(newCommandSet())
    {
    }

    /// Blank card, selected, nothing else done to it.
    bool reset()
    {
        if (!m_backend->resetCard()) {
            m_failure = QStringLiteral("simulator refused RESET");
            return false;
        }
        m_backend->startDetection();
        // The CommandSet caches card state, so a reset card needs a fresh one.
        m_cmdSet = newCommandSet();
        if (!m_cmdSet->select().installed) {
            m_failure = QStringLiteral("select() after reset: %1").arg(m_cmdSet->lastError());
            return false;
        }
        return true;
    }

    bool initialise()
    {
        if (!m_cmdSet->init(Secrets(kSimPin, kSimPuk, kSimPairing))) {
            m_failure = QStringLiteral("init(): %1").arg(m_cmdSet->lastError());
            return false;
        }
        if (!m_cmdSet->select().initialized) {
            m_failure = QStringLiteral("card not initialised after INIT");
            return false;
        }
        if (!m_cmdSet->ensureSecureChannel()) {
            m_failure = QStringLiteral("ensureSecureChannel(): %1").arg(m_cmdSet->lastError());
            return false;
        }
        return true;
    }

    bool blockPin()
    {
        for (int i = 0; i < 3; ++i) {
            m_cmdSet->verifyPIN(kSimWrongPin);
        }
        const ApplicationStatus after = status();
        if (!after.valid) {
            m_failure = QStringLiteral("status unreadable after three wrong PINs, so the "
                                       "card is not demonstrably blocked");
            return false;
        }
        if (after.pinRetryCount != 0) {
            m_failure = QStringLiteral("PIN did not block after three wrong attempts");
            return false;
        }
        return true;
    }

    /// Reads the card's status. Callers must check valid(): getStatus() returns a
    /// default-constructed status on failure, whose pinRetryCount is 0, so an
    /// unchecked read makes "the PIN is blocked" true on a channel that is dead.
    ApplicationStatus status() { return m_cmdSet->getStatus(); }

    /// Re-selects the card without resetting it, for use after factoryReset().
    bool refresh()
    {
        m_cmdSet = newCommandSet();
        if (!m_cmdSet->select().installed) {
            m_failure = QStringLiteral("select() after refresh: %1").arg(m_cmdSet->lastError());
            return false;
        }
        return true;
    }
    CommandSet& cmd() { return *m_cmdSet; }
    SimulatorBackend& backend() { return *m_backend; }
    QString failure() const { return m_failure; }

private:
    /// Pairs on demand from the shared password. Without a provider, init()'s own status
    /// read cannot open a secure channel and the card cannot be provisioned at all.
    std::unique_ptr<CommandSet> newCommandSet()
    {
        return std::make_unique<CommandSet>(
            m_channel, nullptr, [](const QString&) { return kSimPairing; });
    }

    SimulatorBackend* m_backend;
    std::shared_ptr<KeycardChannel> m_channel;
    std::unique_ptr<CommandSet> m_cmdSet;
    QString m_failure;
};

} // namespace Test
} // namespace Keycard

#define QCOMPARE_PIN_RETRIES(card, expected)                                              \
    do {                                                                                  \
        const Keycard::ApplicationStatus s_ = (card).status();                            \
        QVERIFY2(s_.valid, "getStatus() failed, so the retry counter proves nothing");    \
        QCOMPARE(s_.pinRetryCount, uint8_t(expected));                                    \
    } while (0)
