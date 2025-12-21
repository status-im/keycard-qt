#pragma once

#include "keycard-qt/apdu/command.h"
#include <QByteArray>

namespace Keycard {
namespace GlobalPlatform {

/**
 * @brief SCP02 Command Wrapper
 * 
 * Wraps APDU commands with MAC for SCP02 secure channel.
 * Maintains ICV (Initialization Chaining Vector) state across commands.
 * 
 * Architecture:
 * - Stateful: Tracks ICV (last MAC) for MAC chaining
 * - One instance per secure channel session
 * - Thread-safe: Should not be shared across threads
 */
class SCP02Wrapper {
public:
    /**
     * @brief Create a new wrapper with the session ENC and MAC keys
     * @param encKey 16-byte encryption key from session (for ICV encryption)
     * @param macKey 16-byte MAC key from session (for MAC calculation)
     */
    SCP02Wrapper(const QByteArray& encKey, const QByteArray& macKey);
    
    /**
     * @brief Wrap a command with MAC
     * 
     * Process:
     * 1. Set CLA |= 0x04 (indicates MAC follows)
     * 2. Increase Lc by 8 (for MAC)
     * 3. Calculate MAC over: CLA||INS||P1||P2||Lc||Data
     * 4. Append MAC to command data
     * 5. Update ICV for next command
     * 
     * @param cmd Original command
     * @return Wrapped command with MAC appended
     */
    APDU::Command wrap(const APDU::Command& cmd);
    
    /**
     * @brief Reset ICV to null bytes
     * Used when starting a new secure channel
     */
    void reset();
    
private:
    QByteArray m_encKey;  // 16-byte encryption key (for ICV encryption)
    QByteArray m_macKey;  // 16-byte MAC key (for MAC calculation)
    QByteArray m_icv;     // 8-byte Initialization Chaining Vector (last MAC)
};

} // namespace GlobalPlatform
} // namespace Keycard





