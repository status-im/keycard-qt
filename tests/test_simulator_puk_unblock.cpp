// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

/**
 * Unblocking a blocked PIN with the PUK, against the real applet.
 *
 * Skipped when no simulator is listening on 127.0.0.1:9025.
 */

#include <QTest>

#include "sim_fixture.h"

using namespace Keycard;
using namespace Keycard::Test;

class TestSimulatorPukUnblock : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!SimulatorBackend::simulatorAvailable()) {
            QSKIP("keycard-simulator is not listening on 127.0.0.1:9025");
        }
    }

    void testPukUnblocksAndSetsANewPin()
    {
        SimCard card(QStringLiteral("puk"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));
        QVERIFY2(card.blockPin(), qPrintable(card.failure()));

        const QString newPin = QStringLiteral("222222");
        QVERIFY2(card.cmd().unblockPIN(kSimPuk, newPin), qPrintable(card.cmd().lastError()));

        // The applet restores the retry budget as part of unblocking.
        QCOMPARE_PIN_RETRIES(card, 3);

        QVERIFY2(!card.cmd().verifyPIN(kSimPin),
                 "the PIN set at INIT must stop working once it has been replaced");
        QCOMPARE_PIN_RETRIES(card, 2);

        QVERIFY2(card.cmd().verifyPIN(newPin), qPrintable(card.cmd().lastError()));
        QCOMPARE_PIN_RETRIES(card, 3);
    }

    /// Without this the test above would pass against an applet that unblocks on any
    /// PUK at all. The wrong PUK must also cost a PUK attempt, not a PIN one.
    void testWrongPukDoesNotUnblock()
    {
        SimCard card(QStringLiteral("puk-wrong"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));

        const ApplicationStatus before = card.status();
        QVERIFY2(before.valid, "status must be readable before the PUK count is compared");
        QVERIFY2(before.pukRetryCount > 0, "precondition: the card has PUK attempts left");

        QVERIFY2(card.blockPin(), qPrintable(card.failure()));

        QVERIFY2(!card.cmd().unblockPIN(QStringLiteral("111111111111"), QStringLiteral("222222")),
                 "a wrong PUK must not unblock the card");

        const ApplicationStatus after = card.status();
        QVERIFY2(after.valid, "status must be readable after the failed unblock");
        QCOMPARE(after.pinRetryCount, uint8_t(0));
        QCOMPARE(after.pukRetryCount, uint8_t(before.pukRetryCount - 1));
    }
};

QTEST_MAIN(TestSimulatorPukUnblock)
#include "test_simulator_puk_unblock.moc"
