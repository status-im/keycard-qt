#pragma once

#include <QByteArray>
#include <memory>

namespace Keycard {
namespace GlobalPlatform {

/**
 * @brief SCP02 Key pair for encryption and MAC
 * 
 * Encapsulates the encryption and MAC keys used in SCP02 secure channel.
 */
class SCP02Keys {
public:
    SCP02Keys(const QByteArray& encKey, const QByteArray& macKey)
        : m_encKey(encKey), m_macKey(macKey) {}
    
    QByteArray encKey() const { return m_encKey; }
    QByteArray macKey() const { return m_macKey; }
    
private:
    QByteArray m_encKey;  // 16 bytes encryption key
    QByteArray m_macKey;  // 16 bytes MAC key
};

/**
 * @brief SCP02 Session state
 * 
 * Manages the session keys and challenges during SCP02 authentication.
 * This class is created after successful INITIALIZE UPDATE and maintains
 * the session state for subsequent secure channel operations.
 */
class SCP02Session {
public:
    /**
     * @brief Create a new SCP02 session from INITIALIZE UPDATE response
     * 
     * This validates the card cryptogram and derives session keys from
     * the base keys and sequence number received from the card.
     * 
     * @param baseKeys Base encryption/MAC keys (from card defaults or custom)
     * @param initUpdateResponse Response from INITIALIZE UPDATE (28 bytes)
     * @param hostChallenge Our challenge sent to card (8 bytes)
     * @param errorOut Optional error message output
     * @return Session object if successful, nullptr otherwise
     */
    static std::unique_ptr<SCP02Session> create(
        const SCP02Keys& baseKeys,
        const QByteArray& initUpdateResponse,
        const QByteArray& hostChallenge,
        QString* errorOut = nullptr);
    
    /**
     * @brief Get the session keys (derived from base keys and sequence)
     */
    const SCP02Keys& sessionKeys() const { return m_sessionKeys; }
    
    /**
     * @brief Get the card challenge from INITIALIZE UPDATE response
     */
    QByteArray cardChallenge() const { return m_cardChallenge; }
    
    /**
     * @brief Get the host challenge we sent
     */
    QByteArray hostChallenge() const { return m_hostChallenge; }
    
private:
    SCP02Session(const SCP02Keys& sessionKeys, 
                 const QByteArray& cardChallenge,
                 const QByteArray& hostChallenge)
        : m_sessionKeys(sessionKeys)
        , m_cardChallenge(cardChallenge)
        , m_hostChallenge(hostChallenge) {}
    
    SCP02Keys m_sessionKeys;
    QByteArray m_cardChallenge;
    QByteArray m_hostChallenge;
};

} // namespace GlobalPlatform
} // namespace Keycard



