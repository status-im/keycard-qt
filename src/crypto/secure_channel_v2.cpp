#include "keycard-qt/secure_channel_v2.h"
#include "keycard-qt/types.h"
#include <QDebug>
#include <QRandomGenerator>
#include <stdexcept>

namespace Keycard {

QList<QByteArray> defaultTrustedCaKeys()
{
    // Default CA of status-keycard-java (KeycardCommandSet.DEFAULT_CA_PUBLIC_KEY).
    // TODO: confirm the production CA list with the Keycard team before release.
    return { QByteArray::fromHex("029AB99EE1E7A71BDF45B3F9C58C99866FF1294D2C1E304E228A86E10C3343501C") };
}

namespace {
constexpr uint16_t SW_OK = 0x9000;
constexpr int INNER_HEADER_SIZE = 5;  // CLA INS P1 P2 Lc
}

SecureChannelV2::SecureChannelV2(IChannel* channel)
    : m_channel(channel)
{
}

QByteArray SecureChannelV2::prepareOpen()
{
    QMutexLocker locker(&m_mutex);
    closeLocked();

    m_salt = QByteArray(ScV2::SaltSize, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(m_salt.data()),
                                          m_salt.size() / static_cast<int>(sizeof(quint32)));
    m_hostKey = ScV2::generateKeyPair();
    if (!m_hostKey.isValid()) {
        qWarning() << "SecureChannelV2: Failed to generate ephemeral key";
        return QByteArray();
    }
    return m_salt + m_hostKey.publicKey;
}

SecureChannelV2::OpenResult SecureChannelV2::completeOpen(const QByteArray& response,
                                                          const QByteArray& identityPublicKey)
{
    QMutexLocker locker(&m_mutex);
    closeLocked();

    if (!m_hostKey.isValid() || response.size() <= ScV2::PublicKeySize) {
        qWarning() << "SecureChannelV2: Invalid OPEN_SECURE_CHANNEL response, size:" << response.size();
        return OpenResult::InvalidResponse;
    }
    const QByteArray cardPublicKey = response.left(ScV2::PublicKeySize);
    const QByteArray signature = response.mid(ScV2::PublicKeySize);

    const QByteArray digest = ScV2::transcriptDigest(m_salt, m_hostKey.publicKey, cardPublicKey);
    if (!ScV2::verifySignature(identityPublicKey, digest, signature)) {
        qWarning() << "SecureChannelV2: Card transcript signature does not verify";
        return OpenResult::InvalidSignature;
    }

    const QByteArray sharedSecret = ScV2::ecdhSharedSecret(m_hostKey.privateKey, cardPublicKey);
    if (sharedSecret.isEmpty()) {
        qWarning() << "SecureChannelV2: ECDH with the card key failed";
        return OpenResult::InvalidResponse;
    }

    m_keys = ScV2::deriveSessionKeys(m_salt, sharedSecret);
    m_hostKey = ScV2::KeyPair();
    m_salt.clear();
    m_counter = 0;
    m_open = true;
    ++m_session;
    return OpenResult::Opened;
}

void SecureChannelV2::reset()
{
    QMutexLocker locker(&m_mutex);
    closeLocked();
}

void SecureChannelV2::closeLocked()
{
    m_keys = ScV2::SessionKeys();
    m_counter = 0;
    m_open = false;
    ++m_session;
}

bool SecureChannelV2::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_open;
}

APDU::Response SecureChannelV2::send(const APDU::Command& command)
{
    QMutexLocker exchange(&m_exchangeMutex);
    QMutexLocker locker(&m_mutex);

    if (!m_open) {
        throw std::runtime_error("Secure channel not open");
    }
    if (!m_channel) {
        throw std::runtime_error("No base channel available");
    }

    // Inner APDU always carries Lc (00 without data) and never Le
    const QByteArray data = command.data();
    if (INNER_HEADER_SIZE + data.size() > ScV2::MaxPlaintextSize) {
        throw std::runtime_error("Command too large for the secure channel");
    }
    QByteArray inner;
    inner.append(static_cast<char>(command.cla()));
    inner.append(static_cast<char>(command.ins()));
    inner.append(static_cast<char>(command.p1()));
    inner.append(static_cast<char>(command.p2()));
    inner.append(static_cast<char>(data.size()));
    inner.append(data);

    const QByteArray nonce = ScV2::nonce(m_counter);
    const QByteArray wrapped = ScV2::ccmEncrypt(m_keys.hostToCard, nonce, inner);
    if (wrapped.isEmpty()) {
        throw std::runtime_error("Secure channel encryption failed");
    }

    APDU::Command outer(APDU::CLA, APDU::INS_SECURED_APDU, 0x00, 0x00);
    outer.setData(wrapped);
    outer.setLe(0);

    // Same locking as SecureChannel: the lock is released for the transmit so the channel thread
    // can reset() this object on card loss.
    const quint64 session = m_session;
    const QByteArray keyC2H = m_keys.cardToHost;
    const QByteArray serialized = outer.serialize();
    IChannel* channel = m_channel;
    locker.unlock();

    QByteArray rawResponse;
    try {
        rawResponse = channel->transmit(serialized);
    } catch (...) {
        locker.relock();
        // The card may have consumed this nonce; reopen instead of guessing
        if (m_session == session) {
            closeLocked();
        }
        throw;
    }

    locker.relock();
    if (m_session != session || !m_open) {
        throw std::runtime_error("Secure channel was reset during transmit");
    }

    APDU::Response response(rawResponse);
    if (response.sw() != SW_OK) {
        // Plaintext outer error: the card has dropped (or never had) the session
        qWarning() << "SecureChannelV2: Card rejected the secured APDU, SW:"
                   << QString::number(response.sw(), 16);
        closeLocked();
        return response;
    }

    QByteArray plaintext;
    if (!ScV2::ccmDecrypt(keyC2H, nonce, response.data(), plaintext) || plaintext.size() < 2) {
        closeLocked();
        throw std::runtime_error("Response authentication failed");
    }
    ++m_counter;

    // Inner response: [data...][SW1][SW2]
    return APDU::Response(plaintext);
}

} // namespace Keycard
