#pragma once

#include "channel_interface.h"
#include "sc_v2_crypto.h"
#include "apdu/command.h"
#include "apdu/response.h"
#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QRecursiveMutex>

namespace Keycard {

/// CA keys the app trusts for applet 4.x identity certificates (compressed secp256k1)
QList<QByteArray> defaultTrustedCaKeys();

/**
 * @brief SecureChannelV2 of Keycard applet 4.x
 *
 * No pairing: every OPEN_SECURE_CHANNEL is an ephemeral ECDH whose transcript the card signs with
 * its identity key. Commands are wrapped as `80 18 00 00 | CCM(keyH2C, N, CLA INS P1 P2 Lc data)`,
 * replies as `CCM(keyC2H, N, data || SW)`; N is a 13-byte counter, zeroed on open and shared by a
 * command and its reply.
 *
 * Opening is split in two so CommandSet keeps control of the transport:
 * prepareOpen() builds the OPEN_SECURE_CHANNEL payload, completeOpen() checks the card's answer.
 */
class SecureChannelV2 {
public:
    enum class OpenResult {
        Opened,
        InvalidResponse,    ///< Malformed answer / ECDH failure
        InvalidSignature,   ///< Transcript not signed by the card identity key
    };

    explicit SecureChannelV2(IChannel* channel);

    /// New salt + ephemeral key; returns OPEN_SECURE_CHANNEL data (salt || hostPublicKey), empty on failure
    QByteArray prepareOpen();

    /// Verify the card's answer (cardPublicKey || DER signature) and derive the session keys
    OpenResult completeOpen(const QByteArray& response, const QByteArray& identityPublicKey);

    void reset();
    bool isOpen() const;

    /**
     * @brief Wrap, transmit and unwrap one command
     * @return The inner response (data + inner SW). A plaintext outer error (e.g. 6982 tag failure,
     *         6985 no session) is returned as is and closes the channel.
     * @throws std::runtime_error when the channel is not open, the command is too large, the
     *         transmit fails, or the reply does not authenticate (the channel is closed then)
     *
     * Concurrent calls are serialized: a second caller waits until the first exchange is over.
     */
    APDU::Response send(const APDU::Command& command);

#ifdef KEYCARD_ENABLE_TEST_HELPERS
    void testOpenWithKeys(const ScV2::SessionKeys& keys) {
        QMutexLocker locker(&m_mutex);
        m_keys = keys;
        m_counter = 0;
        m_open = true;
        ++m_session;
    }
    quint64 testCounter() const { return m_counter; }
#endif

private:
    void closeLocked();

    IChannel* m_channel = nullptr;
    mutable QRecursiveMutex m_mutex;
    // One exchange at a time: the nonce only advances once a reply is authenticated, so an
    // overlapping send() would encrypt under the same key and nonce. Separate from m_mutex so
    // reset() can still run while a transmit is in flight.
    QMutex m_exchangeMutex;

    QByteArray m_salt;
    ScV2::KeyPair m_hostKey;
    ScV2::SessionKeys m_keys;
    quint64 m_counter = 0;
    bool m_open = false;
    // Bumped on open/reset so send() can tell the session changed while the lock was released
    quint64 m_session = 0;
};

} // namespace Keycard
