// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

/**
 * Integration smoke test against the real Keycard applet.
 *
 * Unlike the rest of the suite, these run against keycard-simulator rather than
 * MockBackend, so the responses come from the applet itself. They are skipped
 * when no simulator is listening.
 *
 * Start one with:
 *   status-keycard-qt/test/keycard-simulator/run.sh 9025
 */

#include <QTest>

#include "sim_fixture.h"

using namespace Keycard;
using namespace Keycard::Test;

class TestSimulatorSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!SimulatorBackend::simulatorAvailable()) {
            QSKIP("keycard-simulator is not listening on 127.0.0.1:9025");
        }
    }

    void testSelectReturnsApplicationInfoFromRealApplet()
    {
        SimCard card(QStringLiteral("smoke"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));

        const ApplicationInfo info = card.cmd().select();

        QVERIFY2(info.installed, qPrintable(card.cmd().lastError()));
        QVERIFY2(!info.secureChannelPublicKey.isEmpty(),
                 "a blank card answers SELECT with its secure-channel public key");
        QVERIFY2(!info.initialized, "a freshly reset card must be uninitialised");
        QVERIFY2(info.instanceUID.isEmpty(),
                 "a blank card does not report an instance uid: the applet holds one from "
                 "construction, but only includes it in SELECT once the card is initialised");
    }

    void testInitThenResetReturnsTheCardToBlank()
    {
        SimCard card(QStringLiteral("smoke-reset"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.cmd().init(Secrets(kSimPin, kSimPuk, kSimPairing)),
                 qPrintable(card.cmd().lastError()));

        const ApplicationInfo afterInit = card.cmd().select(true);
        QVERIFY2(afterInit.initialized, "the card reports initialised after INIT");
        QVERIFY2(!afterInit.instanceUID.isEmpty(), "an initialised card reports its instance uid");

        // The point of RESET: the next test does not inherit this card's state.
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(!card.cmd().select(true).initialized, "RESET must return a blank card");
    }
};

QTEST_MAIN(TestSimulatorSmoke)
#include "test_simulator_smoke.moc"
