// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

/**
 * PIN retry and blocking, against the real applet in keycard-simulator.
 *
 * The counters are the applet's own, so these assertions cannot be satisfied by
 * a mock returning a canned status. Skipped when no simulator is listening;
 * start one with keycard-simulator/run.sh 9025.
 */

#include <QTest>

#include "sim_fixture.h"

using namespace Keycard;
using namespace Keycard::Test;

class TestSimulatorPinBlocking : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!SimulatorBackend::simulatorAvailable()) {
            QSKIP("keycard-simulator is not listening on 127.0.0.1:9025");
        }
    }

    /// The counter is asserted after every attempt, not only at the end, so the test
    /// cannot pass against a card that was already blocked when it started.
    void testThreeWrongPinsBlockTheCard()
    {
        SimCard card(QStringLiteral("pin"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));

        QCOMPARE_PIN_RETRIES(card, 3);

        QVERIFY2(!card.cmd().verifyPIN(kSimWrongPin), "a wrong PIN must not verify");
        QCOMPARE_PIN_RETRIES(card, 2);

        QVERIFY(!card.cmd().verifyPIN(kSimWrongPin));
        QCOMPARE_PIN_RETRIES(card, 1);

        QVERIFY(!card.cmd().verifyPIN(kSimWrongPin));
        QCOMPARE_PIN_RETRIES(card, 0);

        QVERIFY2(!card.cmd().verifyPIN(kSimPin),
                 "once the retry counter reaches zero the correct PIN must also be refused");
        QCOMPARE_PIN_RETRIES(card, 0);
    }

    /// Without this the test above would also pass against a card that only ever
    /// counts down.
    void testCorrectPinRestoresTheRetryCounter()
    {
        SimCard card(QStringLiteral("pin-restore"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));

        QVERIFY(!card.cmd().verifyPIN(kSimWrongPin));
        QCOMPARE_PIN_RETRIES(card, 2);

        QVERIFY2(card.cmd().verifyPIN(kSimPin), qPrintable(card.cmd().lastError()));
        QCOMPARE_PIN_RETRIES(card, 3);
    }

    /// Pins the duress-PIN rule documented in sim_fixture.h: a fixture whose PUK
    /// starts with its PIN would verify through that slot instead of the main one.
    void testPukPrefixVerifiesAsTheDuressPin()
    {
        SimCard card(QStringLiteral("pin-duress"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));

        const QString pukPrefix = kSimPuk.left(6);
        QVERIFY2(pukPrefix != kSimPin, "precondition: the PUK must not start with the PIN");

        QVERIFY2(card.cmd().verifyPIN(pukPrefix),
                 "the first six digits of the PUK verify as the duress PIN");
        QCOMPARE_PIN_RETRIES(card, 3);
    }
};

QTEST_MAIN(TestSimulatorPinBlocking)
#include "test_simulator_pin_blocking.moc"
