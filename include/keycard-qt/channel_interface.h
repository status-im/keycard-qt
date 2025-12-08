#pragma once

#include <QByteArray>

namespace Keycard {

/**
 * @brief Interface for communicating with a smart card/keycard
 * 
 * This interface abstracts the underlying communication mechanism
 * (PC/SC on desktop, NFC on mobile) and provides a simple transmit/receive API.
 */
class IChannel {
public:
    virtual ~IChannel() = default;
    
    /**
     * @brief Transmit an APDU command to the card
     * @param apdu The APDU command bytes
     * @return The APDU response bytes (including SW1/SW2 status word)
     * @throws std::runtime_error if transmission fails
     */
    virtual QByteArray transmit(const QByteArray& apdu) = 0;
    
    /**
     * @brief Check if currently connected to a card
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Force immediate re-scan for cards
     * 
     * Triggers an immediate re-scan for cards. Useful after operations
     * that change card state (e.g., initialization, factory reset).
     * Only supported by backends that implement forceScan().
     * 
     * Default implementation does nothing (for backends that don't support it).
     */
    virtual void forceScan() {}
};

} // namespace Keycard

