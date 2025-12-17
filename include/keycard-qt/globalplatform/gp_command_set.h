#pragma once

#include "keycard-qt/globalplatform/scp02_session.h"
#include "keycard-qt/globalplatform/scp02_wrapper.h"
#include "keycard-qt/apdu/command.h"
#include "keycard-qt/apdu/response.h"
#include "keycard-qt/channel_interface.h"
#include <memory>
#include <QString>
#include <QVector>

namespace Keycard {
namespace GlobalPlatform {

/**
 * @brief GlobalPlatform Command Set
 * 
 * High-level interface for GlobalPlatform operations.
 * Handles SCP02 secure channel establishment and secure command transmission.
 * 
 * Architecture:
 * - Uses dependency injection (IChannel)
 * - Manages SCP02 session lifecycle
 * - Provides clean API for GP operations
 * 
 * Usage:
 * 1. Create with channel
 * 2. select() - Select ISD
 * 3. openSecureChannel() - Establish SCP02
 * 4. deleteObject() / installKeycardApplet() - Perform operations
 */
class GlobalPlatformCommandSet {
public:
    /**
     * @brief Create command set with channel
     * @param channel Communication channel (must outlive this object)
     */
    explicit GlobalPlatformCommandSet(IChannel* channel);
    
    ~GlobalPlatformCommandSet() = default;
    
    /**
     * @brief Select ISD (Issuer Security Domain) or applet
     * @param aid AID to select (empty = select ISD)
     * @return true on success
     */
    bool select(const QByteArray& aid = QByteArray());
    
    /**
     * @brief Open SCP02 secure channel
     * 
     * Process:
     * 1. Generate 8-byte host challenge
     * 2. Send INITIALIZE UPDATE
     * 3. Verify card cryptogram and derive session keys
     * 4. Send EXTERNAL AUTHENTICATE with host cryptogram
     * 
     * Tries multiple key sets in order:
     * - Keycard development key
     * - GlobalPlatform default key (all zeros)
     * 
     * @return true on success
     */
    bool openSecureChannel();
    
    /**
     * @brief Delete applet instance
     * @param aid AID of applet to delete
     * @param deleteRelated If true, also delete related objects (P2=0x80)
     * @return true on success
     */
    bool deleteObject(const QByteArray& aid, bool deleteRelated = false);
    
    /**
     * @brief Install Keycard applet
     * 
     * Note: This assumes the Keycard applet package is already loaded on the card.
     * If the package is not loaded, this will fail.
     * 
     * @return true on success
     */
    bool installKeycardApplet();
    
    /**
     * @brief Get last error message
     */
    QString lastError() const { return m_lastError; }
    
private:
    /**
     * @brief Send INITIALIZE UPDATE command
     * @param hostChallenge 8-byte challenge
     * @return Response (28 bytes on success)
     */
    APDU::Response initializeUpdate(const QByteArray& hostChallenge);
    
    /**
     * @brief Send EXTERNAL AUTHENTICATE command
     * @param hostCryptogram 8-byte cryptogram
     * @return Response (SW=9000 on success)
     */
    APDU::Response externalAuthenticate(const QByteArray& hostCryptogram);
    
    /**
     * @brief Calculate host cryptogram for EXTERNAL AUTHENTICATE
     * @param session Session with keys and challenges
     * @return 8-byte host cryptogram
     */
    QByteArray calculateHostCryptogram(const SCP02Session& session);
    
    /**
     * @brief Send command through secure channel
     * @param cmd Command to send (will be wrapped with MAC)
     * @return Response
     */
    APDU::Response sendSecure(const APDU::Command& cmd);
    
    /**
     * @brief Send command directly (no secure channel)
     * @param cmd Command to send
     * @return Response
     */
    APDU::Response send(const APDU::Command& cmd);
    
    /**
     * @brief Check if response is OK (SW=9000 or allowed SW)
     * @param response Response to check
     * @param allowedSW Additional allowed status words
     * @return true if OK
     */
    bool checkOK(const APDU::Response& response, 
                 const QVector<uint16_t>& allowedSW = QVector<uint16_t>());
    
    IChannel* m_channel;
    std::unique_ptr<SCP02Session> m_session;
    std::unique_ptr<SCP02Wrapper> m_wrapper;
    QString m_lastError;
};

} // namespace GlobalPlatform
} // namespace Keycard

