#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace Keycard {
namespace APDU {

/**
 * @brief Represents an APDU response
 * 
 * APDU response structure: [Data | SW1 | SW2]
 * - Data: Response data (optional)
 * - SW1, SW2: Status word (2 bytes)
 */
class Response {
public:
    /**
     * @brief Construct from raw response bytes
     * @param rawResponse Complete response including SW1/SW2
     */
    explicit Response(const QByteArray& rawResponse);

    /**
     * @brief Initialize from raw response bytes
     * @param rawResponse Complete response including SW1/SW2
     */
    void setData(const QByteArray& rawResponse);
    
    /**
     * @brief Get the response data (without status word)
     * @return The data bytes
     */
    QByteArray data() const { return m_data; }
    
    /**
     * @brief Get the status word
     * @return SW1 << 8 | SW2
     */
    uint16_t sw() const { return m_sw; }
    
    /**
     * @brief Check if response is OK (SW = 0x9000)
     * @return true if OK
     */
    bool isOK() const { return m_sw == 0x9000; }
    
    /**
     * @brief Check if response is a security error
     * @return true if security error
     */
    bool isSecurityError() const;
    
    /**
     * @brief Check if PIN/PUK is blocked
     * @return true if blocked
     */
    bool isBlocked() const;
    
    /**
     * @brief Get remaining PIN/PUK attempts (if wrong PIN/PUK error)
     * @return Remaining attempts, or -1 if not applicable
     */
    int remainingAttempts() const;
    
    /**
     * @brief Get error message for status word
     * @return Human-readable error message
     */
    QString errorMessage() const;
    
    /**
     * @brief Check if this is a wrong PIN error
     * @return true if wrong PIN (0x63Cx)
     */
    bool isWrongPIN() const;
    
    /**
     * @brief Check if this is a wrong PUK error  
     * @return true if wrong PUK (0x63Cx after UnblockPIN)
     */
    bool isWrongPUK() const;
    
private:
    QByteArray m_data;
    uint16_t m_sw;
};

} // namespace APDU
} // namespace Keycard

