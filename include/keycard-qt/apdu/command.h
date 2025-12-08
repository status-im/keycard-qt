#pragma once

#include <QByteArray>
#include <cstdint>

namespace Keycard {
namespace APDU {

/**
 * @brief Represents an APDU command
 * 
 * APDU structure: [CLA | INS | P1 | P2 | Lc | Data | Le]
 * - CLA: Class byte
 * - INS: Instruction byte
 * - P1, P2: Parameter bytes
 * - Lc: Length of data (optional)
 * - Data: Command data (optional)
 * - Le: Expected response length (optional)
 */
class Command {
public:
    /**
     * @brief Construct an APDU command
     * @param cla Class byte
     * @param ins Instruction byte
     * @param p1 Parameter 1
     * @param p2 Parameter 2
     */
    Command(uint8_t cla, uint8_t ins, uint8_t p1 = 0, uint8_t p2 = 0);
    
    /**
     * @brief Set the command data
     * @param data The data bytes to send
     */
    void setData(const QByteArray& data);
    
    /**
     * @brief Set the expected response length
     * @param le Expected length (0 means 256)
     */
    void setLe(uint8_t le);
    
    /**
     * @brief Serialize the command to bytes
     * @return The complete APDU command bytes
     */
    QByteArray serialize() const;
    
    // Getters
    uint8_t cla() const { return m_cla; }
    uint8_t ins() const { return m_ins; }
    uint8_t p1() const { return m_p1; }
    uint8_t p2() const { return m_p2; }
    QByteArray data() const { return m_data; }
    bool hasLe() const { return m_hasLe; }
    uint8_t le() const { return m_le; }
    
private:
    uint8_t m_cla;
    uint8_t m_ins;
    uint8_t m_p1;
    uint8_t m_p2;
    QByteArray m_data;
    bool m_hasLe;
    uint8_t m_le;
};

} // namespace APDU
} // namespace Keycard

