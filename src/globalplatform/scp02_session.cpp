#include "keycard-qt/globalplatform/scp02_session.h"
#include "keycard-qt/globalplatform/gp_crypto.h"
#include "keycard-qt/globalplatform/gp_constants.h"
#include <QDebug>

namespace Keycard {
namespace GlobalPlatform {

std::unique_ptr<SCP02Session> SCP02Session::create(
    const SCP02Keys& baseKeys,
    const QByteArray& initUpdateResponse,
    const QByteArray& hostChallenge,
    QString* errorOut)
{
    // Validate response length (28 bytes expected)
    if (initUpdateResponse.size() != 28) {
        QString error = QString("GP Session: Invalid INITIALIZE UPDATE response length: expected 28, got %1")
            .arg(initUpdateResponse.size());
        qWarning() << error;
        if (errorOut) *errorOut = error;
        return nullptr;
    }
    
    // Validate host challenge
    if (hostChallenge.size() != 8) {
        QString error = QString("GP Session: Invalid host challenge length: expected 8, got %1")
            .arg(hostChallenge.size());
        qWarning() << error;
        if (errorOut) *errorOut = error;
        return nullptr;
    }
    
    // Parse response:
    // [0:10]   - Key diversification data (not used in basic SCP02)
    // [10]     - Key information
    // [11]     - SCP version (must be 2)
    // [12:14]  - Sequence counter
    // [14:20]  - Card challenge
    // [20:28]  - Card cryptogram
    
    uint8_t scpVersion = static_cast<uint8_t>(initUpdateResponse[11]);
    if (scpVersion != 2) {
        QString error = QString("GP Session: Unsupported SCP version: %1 (only SCP02 is supported)")
            .arg(scpVersion);
        qWarning() << error;
        if (errorOut) *errorOut = error;
        return nullptr;
    }
    
    // Extract sequence counter (2 bytes)
    QByteArray sequence = initUpdateResponse.mid(12, 2);
    
    // Extract card challenge (8 bytes)
    QByteArray cardChallenge = initUpdateResponse.mid(12, 8);
    
    // Extract card cryptogram (8 bytes)
    QByteArray cardCryptogram = initUpdateResponse.mid(20, 8);
    
    qDebug() << "GP Session: Sequence:" << sequence.toHex();
    qDebug() << "GP Session: Card challenge:" << cardChallenge.toHex();
    qDebug() << "GP Session: Card cryptogram:" << cardCryptogram.toHex();
    
    // Derive session keys from base keys and sequence
    QByteArray sessionEncKey = Crypto::deriveKey(
        baseKeys.encKey(), 
        sequence, 
        Crypto::DERIVATION_PURPOSE_ENC());
    
    QByteArray sessionMacKey = Crypto::deriveKey(
        baseKeys.encKey(),  // Note: Go uses ENC key for both derivations
        sequence,
        Crypto::DERIVATION_PURPOSE_MAC());
    
    if (sessionEncKey.isEmpty() || sessionMacKey.isEmpty()) {
        QString error = "GP Session: Failed to derive session keys";
        qWarning() << error;
        if (errorOut) *errorOut = error;
        return nullptr;
    }
    
    qDebug() << "GP Session: Derived session ENC key:" << sessionEncKey.left(8).toHex() << "...";
    qDebug() << "GP Session: Derived session MAC key:" << sessionMacKey.left(8).toHex() << "...";
    
    // Create session keys
    SCP02Keys sessionKeys(sessionEncKey, sessionMacKey);
    
    // Verify card cryptogram
    bool verified = Crypto::verifyCryptogram(
        sessionKeys.encKey(),
        hostChallenge,
        cardChallenge,
        cardCryptogram);
    
    if (!verified) {
        QString error = "GP Session: Card cryptogram verification failed - incorrect keys or card not authentic";
        qWarning() << error;
        if (errorOut) *errorOut = error;
        return nullptr;
    }
    
    qDebug() << "GP Session: Card cryptogram verified successfully";
    
    // Create session (using private constructor)
    return std::unique_ptr<SCP02Session>(
        new SCP02Session(sessionKeys, cardChallenge, hostChallenge));
}

} // namespace GlobalPlatform
} // namespace Keycard







