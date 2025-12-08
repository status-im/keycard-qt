#pragma once

#include "../types.h"
#include <QByteArray>
#include <QString>

namespace Keycard {

/**
 * @brief Application information returned by SELECT command
 */
class ApplicationInfo {
public:
    ApplicationInfo() = default;
    
    /**
     * @brief Parse application info from SELECT response data
     * @param data Raw response data
     * @return Parsed ApplicationInfo or error
     */
    static ApplicationInfo parse(const QByteArray& data, QString* error = nullptr);
    
    // Getters
    bool isInstalled() const { return m_installed; }
    bool isInitialized() const { return m_initialized; }
    QByteArray instanceUID() const { return m_instanceUID; }
    QByteArray secureChannelPublicKey() const { return m_secureChannelPublicKey; }
    QByteArray version() const { return m_version; }
    QByteArray availableSlots() const { return m_availableSlots; }
    QByteArray keyUID() const { return m_keyUID; }
    Capability capabilities() const { return m_capabilities; }
    
    // Capability checks
    bool hasCapability(Capability cap) const {
        return Keycard::hasCapability(m_capabilities, cap);
    }
    
    bool hasSecureChannelCapability() const {
        return hasCapability(Capability::SecureChannel);
    }
    
    bool hasKeyManagementCapability() const {
        return hasCapability(Capability::KeyManagement);
    }
    
    bool hasCredentialsManagementCapability() const {
        return hasCapability(Capability::CredentialsManagement);
    }
    
    bool hasNDEFCapability() const {
        return hasCapability(Capability::NDEF);
    }
    
    bool hasFactoryResetCapability() const {
        return hasCapability(Capability::FactoryReset);
    }
    
private:
    bool m_installed = false;
    bool m_initialized = false;
    QByteArray m_instanceUID;
    QByteArray m_secureChannelPublicKey;
    QByteArray m_version;
    QByteArray m_availableSlots;
    QByteArray m_keyUID;
    Capability m_capabilities = Capability::None;
};

} // namespace Keycard

