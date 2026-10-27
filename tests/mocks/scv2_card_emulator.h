#pragma once

#include "keycard-qt/sc_v2_crypto.h"
#include <QByteArray>
#include <QList>
#include <QString>

namespace Keycard {
namespace Test {

/**
 * @brief Card side of the applet 4.x SecureChannelV2, for MockBackend::setResponseHandler()
 *
 * Answers SELECT with a 4.x application info template, OPEN_SECURE_CHANNEL with a signed
 * ephemeral key, and SECURED APDU (0x18) with real AES-128-CCM. Inner commands get a small
 * applet model (INIT, GET STATUS, VERIFY PIN, SIGN, EXPORT KEY, GET/STORE DATA) and are recorded.
 */
class ScV2CardEmulator {
public:
    struct InnerCommand {
        uint8_t ins = 0;
        uint8_t p1 = 0;
        uint8_t p2 = 0;
        QByteArray data;
    };

    explicit ScV2CardEmulator(const QByteArray& caPrivateKey);

    QByteArray handle(const QByteArray& apdu);

    QByteArray caPublicKey() const { return m_caPublicKey; }
    QByteArray certificate() const { return m_certificate; }
    QByteArray identityPublicKey() const { return m_identityPublicKey; }
    QByteArray selectResponse() const;

    // Card state
    bool initialized = false;
    QByteArray keyUID;
    int pinRetries = 3;
    int pukRetries = 5;
    QString pin;
    QString puk;

    // Fault injection
    bool signWrongTranscript = false;  ///< OPEN answer signs a different transcript
    bool rejectNextSecured = false;    ///< Next SECURED APDU answers a plaintext 6982

    // Observations
    QList<InnerCommand> innerCommands;
    int openCount = 0;
    quint64 counter() const { return m_counter; }
    bool isOpen() const { return m_open; }

private:
    QByteArray openSecureChannel(const QByteArray& data);
    QByteArray securedApdu(const QByteArray& data);
    QByteArray processInner(const InnerCommand& command);

    QByteArray m_caPublicKey;
    ScV2::KeyPair m_identity;
    QByteArray m_identityPublicKey;
    QByteArray m_certificate;

    ScV2::SessionKeys m_keys;
    quint64 m_counter = 0;
    bool m_open = false;
};

} // namespace Test
} // namespace Keycard
