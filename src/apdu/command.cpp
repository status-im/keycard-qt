#include "keycard-qt/apdu/command.h"

namespace Keycard {
namespace APDU {

Command::Command(uint8_t cla, uint8_t ins, uint8_t p1, uint8_t p2)
    : m_cla(cla)
    , m_ins(ins)
    , m_p1(p1)
    , m_p2(p2)
    , m_hasLe(false)
    , m_le(0)
{
}

void Command::setData(const QByteArray& data)
{
    m_data = data;
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
    
    // Data field
    if (!m_data.isEmpty()) {
        // Lc (length of data)
        if (m_data.size() <= 255) {
            // Short form
            result.append(static_cast<char>(m_data.size()));
        } else {
            // Extended form (not commonly used with keycards)
            result.append(static_cast<char>(0));
            result.append(static_cast<char>((m_data.size() >> 8) & 0xFF));
            result.append(static_cast<char>(m_data.size() & 0xFF));
        }
        
        // Data
        result.append(m_data);
    }
    
    // Le (expected response length)
    if (m_hasLe) {
        result.append(static_cast<char>(m_le));
    }
    
    return result;
}

} // namespace APDU
} // namespace Keycard

