#pragma once

#include "types.h"
#include <QString>

namespace Keycard {

/**
 * @brief Interface for pairing data persistence
 * 
 * Allows CommandSet to load/save pairing information without
 * knowing about the underlying storage mechanism (file, database, etc.)
 */
class IPairingStorage {
public:
    virtual ~IPairingStorage() = default;
    
    /**
     * @brief Load pairing info for a specific card
     * @param cardInstanceUID The card's instance UID (hex string)
     * @return PairingInfo if found, invalid PairingInfo otherwise
     */
    virtual PairingInfo load(const QString& cardInstanceUID) = 0;
    
    /**
     * @brief Save pairing info for a specific card
     * @param cardInstanceUID The card's instance UID (hex string)
     * @param pairing The pairing info to save
     * @return true if saved successfully, false otherwise
     */
    virtual bool save(const QString& cardInstanceUID, const PairingInfo& pairing) = 0;
    
    /**
     * @brief Remove pairing info for a specific card
     * @param cardInstanceUID The card's instance UID (hex string)
     * @return true if removed successfully, false otherwise
     */
    virtual bool remove(const QString& cardInstanceUID) = 0;
};

} // namespace Keycard

