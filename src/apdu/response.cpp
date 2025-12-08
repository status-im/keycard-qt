#include "keycard-qt/apdu/response.h"

namespace Keycard {
namespace APDU {

Response::Response(const QByteArray& rawResponse)
    : m_sw(0)
{
    setData(rawResponse);
}

void Response::setData(const QByteArray& rawResponse)
{
    if (rawResponse.size() < 2) {
        // Invalid response - should have at least SW1 SW2
        m_sw = 0x6F00; // Unknown error
        return;
    }
    
    // Last 2 bytes are SW1 and SW2
    int dataLen = rawResponse.size() - 2;
    if (dataLen > 0) {
        m_data = rawResponse.left(dataLen);
    }
    
    uint8_t sw1 = static_cast<uint8_t>(rawResponse[dataLen]);
    uint8_t sw2 = static_cast<uint8_t>(rawResponse[dataLen + 1]);
    m_sw = (static_cast<uint16_t>(sw1) << 8) | sw2;
}

bool Response::isSecurityError() const
{
    return m_sw == 0x6982 ||  // Security condition not satisfied
           m_sw == 0x6983 ||  // Authentication method blocked
           m_sw == 0x6984 ||  // Data invalid
           m_sw == 0x6985;    // Conditions not satisfied
}

bool Response::isBlocked() const
{
    return m_sw == 0x6983; // Authentication method blocked
}

int Response::remainingAttempts() const
{
    // Check for 0x63Cx pattern (remaining attempts)
    if ((m_sw & 0xFFF0) == 0x63C0) {
        return m_sw & 0x000F;
    }
    return -1; // Not applicable
}

QString Response::errorMessage() const
{
    switch (m_sw) {
    case 0x9000:
        return QStringLiteral("Success");
    case 0x6982:
        return QStringLiteral("Security condition not satisfied");
    case 0x6983:
        return QStringLiteral("Authentication method blocked");
    case 0x6984:
        return QStringLiteral("Data invalid");
    case 0x6985:
        return QStringLiteral("Conditions not satisfied");
    case 0x6A80:
        return QStringLiteral("Wrong data");
    case 0x6A82:
        return QStringLiteral("File not found");
    case 0x6A84:
        return QStringLiteral("No available pairing slots");
    case 0x6A86:
        return QStringLiteral("Incorrect P1/P2");
    case 0x6A88:
        return QStringLiteral("Referenced data not found");
    case 0x6700:
        return QStringLiteral("Wrong length");
    case 0x6D00:
        return QStringLiteral("Instruction not supported");
    case 0x6E00:
        return QStringLiteral("Class not supported");
    default:
        if ((m_sw & 0xFFF0) == 0x63C0) {
            int attempts = m_sw & 0x000F;
            return QStringLiteral("Wrong PIN/PUK. Remaining attempts: %1").arg(attempts);
        }
        return QStringLiteral("Unknown error: 0x%1").arg(m_sw, 4, 16, QLatin1Char('0'));
    }
}

bool Response::isWrongPIN() const
{
    // 0x63Cx pattern indicates wrong PIN with x remaining attempts
    return (m_sw & 0xFFF0) == 0x63C0;
}

bool Response::isWrongPUK() const
{
    // Same pattern as PIN, but context-dependent
    // The caller needs to know if this was from an UnblockPIN command
    return (m_sw & 0xFFF0) == 0x63C0;
}

} // namespace APDU
} // namespace Keycard

