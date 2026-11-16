// Copyright (C) 2026 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

/**
 * Factory reset and seed recovery, against the real applet.
 *
 * Skipped when no simulator is listening on 127.0.0.1:9025.
 */

#include <QTest>

#include "sim_fixture.h"

using namespace Keycard;
using namespace Keycard::Test;

namespace {
/// A fixed 64-byte BIP39 seed, so the key uid it produces is reproducible.
QByteArray testSeed()
{
    return QByteArray::fromHex(
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f");
}
}

class TestSimulatorFactoryReset : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!SimulatorBackend::simulatorAvailable()) {
            QSKIP("keycard-simulator is not listening on 127.0.0.1:9025");
        }
    }

    void testFactoryResetClearsTheCard()
    {
        SimCard card(QStringLiteral("factory"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));
        QVERIFY2(card.cmd().verifyPIN(kSimPin), qPrintable(card.cmd().lastError()));
        QVERIFY2(!card.cmd().loadSeed(testSeed()).isEmpty(), qPrintable(card.cmd().lastError()));

        const ApplicationStatus loaded = card.status();
        QVERIFY2(loaded.valid, "status must be readable before the key is asserted");
        QVERIFY2(loaded.keyInitialized, "the card reports a key loaded");

        QVERIFY2(card.cmd().factoryReset(), qPrintable(card.cmd().lastError()));

        // select(true) forces a re-read: CommandSet otherwise answers from its cache.
        const ApplicationInfo after = card.cmd().select(true);
        QVERIFY2(!after.initialized, "a factory-reset card must report itself uninitialised");
        QVERIFY2(after.keyUID.isEmpty(), "a factory-reset card must not still report a key uid");
    }

    /// The card is blanked by factoryReset() alone here, deliberately: resetting it
    /// through the simulator instead would make this pass without the applet's own
    /// reset ever working.
    void testSeedRecoveryAfterFactoryResetRestoresTheSameKey()
    {
        SimCard card(QStringLiteral("recovery"));
        QVERIFY2(card.reset(), qPrintable(card.failure()));
        QVERIFY2(card.initialise(), qPrintable(card.failure()));
        QVERIFY2(card.cmd().verifyPIN(kSimPin), qPrintable(card.cmd().lastError()));
        const QByteArray originalKeyUID = card.cmd().loadSeed(testSeed());
        QVERIFY2(!originalKeyUID.isEmpty(), qPrintable(card.cmd().lastError()));

        QVERIFY2(card.blockPin(), qPrintable(card.failure()));
        QVERIFY2(card.cmd().factoryReset(), qPrintable(card.cmd().lastError()));

        // Re-select the card the applet just blanked. No simulator RESET here.
        QVERIFY2(card.refresh(), qPrintable(card.failure()));
        QVERIFY2(!card.cmd().select(true).initialized,
                 "factoryReset() must be what left the card blank");

        QVERIFY2(card.initialise(), qPrintable(card.failure()));
        QVERIFY2(card.cmd().verifyPIN(kSimPin), qPrintable(card.cmd().lastError()));
        const QByteArray recoveredKeyUID = card.cmd().loadSeed(testSeed());

        QVERIFY2(!recoveredKeyUID.isEmpty(), qPrintable(card.cmd().lastError()));
        QCOMPARE(recoveredKeyUID, originalKeyUID);
        QCOMPARE_PIN_RETRIES(card, 3);
    }
};

QTEST_MAIN(TestSimulatorFactoryReset)
#include "test_simulator_factory_reset.moc"
