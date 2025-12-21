#include "keycard-qt/apdu/command.h"

namespace Keycard {
namespace APDU {

Command::Command(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2)
    : m_cla(cla)
    , m_ins(ins)
    , m_p1(p1)
    , m_p2(p2)
    , m_hasData(false)
    , m_hasLe(false)
    , m_le(0)
{
}

void Command::setData(const QByteArray& data)
{
    m_data = data;
    m_hasData = true;  // Track that setData was called, even if data is empty
}

void Command::setLe(uint8_t le)
{
    m_hasLe = true;
    m_le = le;
}

QByteArray Command::serialize() const
{
    QByteArray result;
    
    // Header: CLA | INS | P1 | P2
    result.append(static_cast<char>(m_cla));
    result.append(static_cast<char>(m_ins));
    result.append(static_cast<char>(m_p1));
    result.append(static_cast<char>(m_p2));
    
    // Determine APDU case based on whether setData/setLe were called
    // Match Java behavior: always output Lc if setData was called, even with empty data
    if (m_hasData || m_hasLe) {
        // Case 3 or 4: Output Lc (even if 0)
        // This is critical for iOS CoreNFC compatibility with GlobalPlatform
        // Java always writes data.length, even when data array is empty
        
        if (m_data.size() <= 255) {
            // Short form Lc
            result.append(static_cast<char>(m_data.size()));
        } else {
            // Extended form (not commonly used with keycards)
            result.append(static_cast<char>(0));
            result.append(static_cast<char>((m_data.size() >> 8) & 0xFF));
            result.append(static_cast<char>(m_data.size() & 0xFF));
        }
        
        // Data (if any)
        if (!m_data.isEmpty()) {
            result.append(m_data);
        }
        
        // Le (expected response length) - Case 4
        if (m_hasLe) {
            result.append(static_cast<char>(m_le));
        }
    }
    // else: Case 1 (header only) - neither setData nor setLe called
    
    return result;
}

} // namespace APDU
} // namespace Keycard

